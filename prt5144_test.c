#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include "ecrt.h"

// From `ethercat cstruct -p 0`: Master 0, Slave 0, "PRT.5144" (Revision 0x00001000)
#define PRT5144_VENDOR_ID    0x000001a1
#define PRT5144_PRODUCT_CODE 0x00000032

#define CYCLE_US 1000 // 1 ms cycle time (1 kHz)
#define NSEC_PER_SEC 1000000000L
#define PREFAULT_STACK_SIZE (64 * 1024)
#define SHUTDOWN_CYCLES 10 // cycles of zeroed outputs sent before releasing the master

static volatile sig_atomic_t run = 1;

static ec_master_t *master = NULL;
static ec_domain_t *domain1 = NULL;
static ec_slave_config_t *sc = NULL;
static uint8_t *domain1_pd = NULL;

// Byte and bit offsets populated by ecrt_domain_reg_pdo_entry_list()
static unsigned int off_out[8];
static unsigned int bit_out[8];
static unsigned int off_in[8];
static unsigned int bit_in[8];

static const ec_pdo_entry_reg_t domain1_regs[] = {
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x7000, 0x01, &off_out[0], &bit_out[0]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x7010, 0x01, &off_out[1], &bit_out[1]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x7020, 0x01, &off_out[2], &bit_out[2]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x7030, 0x01, &off_out[3], &bit_out[3]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x7040, 0x01, &off_out[4], &bit_out[4]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x7050, 0x01, &off_out[5], &bit_out[5]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x7060, 0x01, &off_out[6], &bit_out[6]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x7070, 0x01, &off_out[7], &bit_out[7]},

    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x6000, 0x01, &off_in[0], &bit_in[0]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x6010, 0x01, &off_in[1], &bit_in[1]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x6020, 0x01, &off_in[2], &bit_in[2]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x6030, 0x01, &off_in[3], &bit_in[3]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x6040, 0x01, &off_in[4], &bit_in[4]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x6050, 0x01, &off_in[5], &bit_in[5]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x6060, 0x01, &off_in[6], &bit_in[6]},
    {0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE, 0x6070, 0x01, &off_in[7], &bit_in[7]},
    {}
};

static const ec_pdo_entry_info_t slave_0_pdo_entries[] = {
    {0x7000, 0x01, 1}, {0x7010, 0x01, 1}, {0x7020, 0x01, 1}, {0x7030, 0x01, 1},
    {0x7040, 0x01, 1}, {0x7050, 0x01, 1}, {0x7060, 0x01, 1}, {0x7070, 0x01, 1},
    {0x6000, 0x01, 1}, {0x6010, 0x01, 1}, {0x6020, 0x01, 1}, {0x6030, 0x01, 1},
    {0x6040, 0x01, 1}, {0x6050, 0x01, 1}, {0x6060, 0x01, 1}, {0x6070, 0x01, 1},
};

static const ec_pdo_info_t slave_0_pdos[] = {
    {0x1600, 1, slave_0_pdo_entries + 0},
    {0x1601, 1, slave_0_pdo_entries + 1},
    {0x1602, 1, slave_0_pdo_entries + 2},
    {0x1603, 1, slave_0_pdo_entries + 3},
    {0x1604, 1, slave_0_pdo_entries + 4},
    {0x1605, 1, slave_0_pdo_entries + 5},
    {0x1606, 1, slave_0_pdo_entries + 6},
    {0x1607, 1, slave_0_pdo_entries + 7},
    {0x1a00, 1, slave_0_pdo_entries + 8},
    {0x1a01, 1, slave_0_pdo_entries + 9},
    {0x1a02, 1, slave_0_pdo_entries + 10},
    {0x1a03, 1, slave_0_pdo_entries + 11},
    {0x1a04, 1, slave_0_pdo_entries + 12},
    {0x1a05, 1, slave_0_pdo_entries + 13},
    {0x1a06, 1, slave_0_pdo_entries + 14},
    {0x1a07, 1, slave_0_pdo_entries + 15},
};

static const ec_sync_info_t slave_0_syncs[] = {
    {0, EC_DIR_OUTPUT, 8, slave_0_pdos + 0, EC_WD_ENABLE},
    {1, EC_DIR_INPUT,  8, slave_0_pdos + 8, EC_WD_DISABLE},
    {0xff}
};

static void sig_handler(int sig) {
    (void)sig;
    run = 0;
}

static inline void timespec_add_us(struct timespec *t, long us) {
    t->tv_nsec += us * 1000L;
    while (t->tv_nsec >= NSEC_PER_SEC) {
        t->tv_nsec -= NSEC_PER_SEC;
        t->tv_sec++;
    }
}

static void stack_prefault(void) {
    unsigned char dummy[PREFAULT_STACK_SIZE];
    memset(dummy, 0, sizeof(dummy));
}

static void wait_next_cycle(struct timespec *wakeup_time) {
    timespec_add_us(wakeup_time, CYCLE_US);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, wakeup_time, NULL) == EINTR) {
        // Interrupted by a signal: resume sleeping until the absolute wakeup time
    }
}

static void check_domain_state(void) {
    static ec_domain_state_t last = { .working_counter = ~0u, .wc_state = (ec_wc_state_t)-1 };
    ec_domain_state_t ds;
    ecrt_domain_state(domain1, &ds);

    if (ds.working_counter != last.working_counter) {
        printf("Domain1: WC %u\n", ds.working_counter);
    }
    if (ds.wc_state != last.wc_state) {
        printf("Domain1: State %u (%s)\n", ds.wc_state,
               ds.wc_state == EC_WC_COMPLETE ? "complete" :
               ds.wc_state == EC_WC_INCOMPLETE ? "incomplete" : "zero");
    }
    last = ds;
}

static void check_master_state(void) {
    static int first = 1;
    static ec_master_state_t last;
    ec_master_state_t ms;
    ecrt_master_state(master, &ms);

    if (first || ms.slaves_responding != last.slaves_responding) {
        printf("Master: %u slave(s) responding\n", ms.slaves_responding);
    }
    if (first || ms.al_states != last.al_states) {
        printf("Master: AL states 0x%02X\n", ms.al_states);
    }
    if (first || ms.link_up != last.link_up) {
        printf("Master: Link is %s\n", ms.link_up ? "up" : "down");
    }
    first = 0;
    last = ms;
}

static void check_slave_config_state(void) {
    // al_state/online/operational are bitfields, so use a flag to force the first report
    static int first = 1;
    static ec_slave_config_state_t last;
    ec_slave_config_state_t s;
    ecrt_slave_config_state(sc, &s);

    if (first || s.al_state != last.al_state) {
        printf("PRT5144: AL state 0x%02X\n", s.al_state);
    }
    if (first || s.online != last.online) {
        printf("PRT5144: %s\n", s.online ? "online" : "offline");
    }
    if (first || s.operational != last.operational) {
        printf("PRT5144: %soperational\n", s.operational ? "" : "not ");
    }
    first = 0;
    last = s;
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Lock memory and configure PREEMPT_RT FIFO scheduling
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall failed (needs root or CAP_IPC_LOCK)");
    }
    struct sched_param param = { .sched_priority = 80 };
    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        perror("sched_setscheduler failed (continuing without RT priority)");
    }

    master = ecrt_request_master(0);
    if (!master) {
        fprintf(stderr, "Failed to request master 0\n");
        return 1;
    }

    domain1 = ecrt_master_create_domain(master);
    if (!domain1) {
        fprintf(stderr, "Failed to create domain\n");
        ecrt_release_master(master);
        return 1;
    }

    sc = ecrt_master_slave_config(master, 0, 0, PRT5144_VENDOR_ID, PRT5144_PRODUCT_CODE);
    if (!sc) {
        fprintf(stderr, "Failed to get slave configuration\n");
        ecrt_release_master(master);
        return 1;
    }

    if (ecrt_slave_config_pdos(sc, EC_END, slave_0_syncs)) {
        fprintf(stderr, "Failed to configure PDOs\n");
        ecrt_release_master(master);
        return 1;
    }

    if (ecrt_domain_reg_pdo_entry_list(domain1, domain1_regs)) {
        fprintf(stderr, "PDO entry registration failed\n");
        ecrt_release_master(master);
        return 1;
    }

    if (ecrt_master_activate(master)) {
        fprintf(stderr, "Failed to activate master\n");
        ecrt_release_master(master);
        return 1;
    }

    domain1_pd = ecrt_domain_data(domain1);
    if (!domain1_pd) {
        fprintf(stderr, "Failed to get domain data pointer\n");
        ecrt_release_master(master);
        return 1;
    }

    stack_prefault();

    printf("EtherCAT Master active. Press Ctrl+C to stop.\n");

    struct timespec wakeup_time;
    clock_gettime(CLOCK_MONOTONIC, &wakeup_time);

    unsigned int cycle_count = 0;
    unsigned int status_count = 0;
    unsigned int active_out = 0;
    int last_inputs = -1; // -1 forces printing the first valid input state

    while (run) {
        wait_next_cycle(&wakeup_time);

        // 1. Receive process data
        ecrt_master_receive(master);
        ecrt_domain_process(domain1);

        // 2. Report bus/slave state changes (every 100 ms)
        if (++status_count >= 100) {
            status_count = 0;
            check_master_state();
            check_slave_config_state();
        }
        check_domain_state();

        // 3. Read all 8 digital inputs (only trust them when the WC is complete)
        ec_domain_state_t ds;
        ecrt_domain_state(domain1, &ds);
        if (ds.wc_state == EC_WC_COMPLETE) {
            uint8_t inputs = 0;
            for (int i = 0; i < 8; i++) {
                if (EC_READ_BIT(domain1_pd + off_in[i], bit_in[i])) {
                    inputs |= (uint8_t)(1u << i);
                }
            }
            if ((int)inputs != last_inputs) {
                printf("Inputs changed: 0x%02X (IN7..IN0: %d%d%d%d%d%d%d%d)\n",
                       inputs,
                       (inputs >> 7) & 1, (inputs >> 6) & 1, (inputs >> 5) & 1, (inputs >> 4) & 1,
                       (inputs >> 3) & 1, (inputs >> 2) & 1, (inputs >> 1) & 1, (inputs >> 0) & 1);
                last_inputs = inputs;
            }
        }

        // 4. Advance the active output every 500 ms (500 cycles at 1 kHz)
        if (++cycle_count >= 500) {
            cycle_count = 0;
            active_out = (active_out + 1) % 8;
        }
        for (int i = 0; i < 8; i++) {
            EC_WRITE_BIT(domain1_pd + off_out[i], bit_out[i], (i == (int)active_out) ? 1 : 0);
        }

        // 5. Send process data
        ecrt_domain_queue(domain1);
        ecrt_master_send(master);
    }

    // Clear outputs on exit: run several full cycles so the zeroed frame is actually
    // exchanged (a datagram still awaiting its reply cannot be re-queued).
    for (int n = 0; n < SHUTDOWN_CYCLES; n++) {
        wait_next_cycle(&wakeup_time);
        ecrt_master_receive(master);
        ecrt_domain_process(domain1);
        for (int i = 0; i < 8; i++) {
            EC_WRITE_BIT(domain1_pd + off_out[i], bit_out[i], 0);
        }
        ecrt_domain_queue(domain1);
        ecrt_master_send(master);
    }
    ecrt_release_master(master);

    printf("Stopped cleanly.\n");
    return 0;
}
