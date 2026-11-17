# Lab Diary: IgH EtherCAT Master Setup on Debian 13 (PREEMPT_RT)

**System:** T8PLUS (`6.12.74+deb13+1-rt-amd64`, Legacy BIOS / Secure Boot inactive)  
**EtherCAT Master:** IgH EtherCAT Master `1.6.13` (Generic NIC driver)  
**Network Interface:** `enp3s0` (MAC: `68:1d:ef:38:e7:3e`)  
**Connected Slave:** `PRT5144_SII_1v00` (8x Digital Out, 8x Digital In)

---

## 1. Kernel Verification & Build Dependencies
Verified the active real-time kernel and installed the matching Debian 13 RT headers and build tools:

```bash
uname -r
# Output: 6.12.74+deb13+1-rt-amd64

sudo apt update
sudo apt install -y build-essential autoconf automake libtool pkg-config linux-headers-6.12.74+deb13+1-rt-amd64
```

---

## 2. Compile & Install IgH EtherCAT Master 1.6.13
Built from the release tarball (`~/sw-dev/ethercat-1.6.13`) using the `generic` network driver:

```bash
cd ~/sw-dev/ethercat-1.6.13
./configure --prefix=/usr/local --with-linux-dir=/lib/modules/6.12.74+deb13+1-rt-amd64/build --enable-generic --disable-8139too
make all modules
sudo make modules_install install
sudo depmod -a
```
*(Note: Out-of-tree module signing SSL warnings during `modules_install` are expected on Debian and harmless since Legacy BIOS mode is active and Secure Boot is off.)*

---

## 3. Network Isolation & Master Configuration
Identified `enp3s0` (`68:1d:ef:38:e7:3e`) as the physical EtherCAT port, isolated it from NetworkManager while keeping the link `UP`, and configured the master:

```bash
# Isolate NIC from NetworkManager and bring link up
sudo nmcli dev set enp3s0 managed no
sudo ip link set enp3s0 up

# Configure EtherCAT master parameters
sudo mkdir -p /etc/sysconfig /usr/local/etc/sysconfig
sudo cp /usr/local/etc/ethercat.conf /etc/ethercat.conf
sudo sed -i 's/^MASTER0_DEVICE=.*/MASTER0_DEVICE="68:1d:ef:38:e7:3e"/' /etc/ethercat.conf
sudo sed -i 's/^DEVICE_MODULES=.*/DEVICE_MODULES="generic"/' /etc/ethercat.conf
sudo cp /etc/ethercat.conf /etc/sysconfig/ethercat
sudo cp /etc/ethercat.conf /usr/local/etc/sysconfig/ethercat

# Set udev permissions for /dev/EtherCAT0 and link CLI/library paths
echo 'KERNEL=="EtherCAT[0-9]*", MODE="0666"' | sudo tee /etc/udev/rules.d/99-ethercat.rules
sudo udevadm control --reload-rules
sudo ln -sf /usr/local/bin/ethercat /usr/bin/ethercat
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/ethercat.conf
sudo ldconfig
```

---

## 4. Service Setup & Bus Verification
Registered the SysV init script with `systemd`, started the master, and verified communication with the slave:

```bash
sudo cp /usr/local/etc/init.d/ethercat /etc/init.d/ethercat
sudo chmod +x /etc/init.d/ethercat
sudo systemctl daemon-reload
sudo systemctl enable ethercat
sudo /etc/init.d/ethercat start
sudo systemctl restart ethercat
```

### Verification Results
* `ethercat master`: Confirmed `Main: 68:1d:ef:38:e7:3e (attached)`, `Link: UP`, `0.0%` frame loss.
* `ethercat slaves`: Confirmed `0  0:0  PREOP  +  PRT5144_SII_1v00`.
* `ethercat pdos`:
  * **SM0 (Outputs):** `RxPDO 0x1600`–`0x1607` (`0x7000:01`–`0x7070:01`, 1 bit each: `OUT0`–`OUT7`).
  * **SM1 (Inputs):** `TxPDO 0x1a00`–`0x1a07` (`0x6000:01`–`0x6070:01`, 1 bit each: `IN0`–`IN7`).

---

## 5. C/C++ Compilation & Real-Time Execution Notes

### 5.1 About `ecrt.h` and `libethercat`
`ecrt.h` ("EtherCAT RealTime") is the IgH master's **application interface**. `make install` put it in `/usr/local/include/ecrt.h`. It declares:
* **Types** that describe the bus and the slave layout: `ec_master_t`, `ec_domain_t`, `ec_slave_config_t`, `ec_pdo_entry_info_t`, `ec_pdo_info_t`, `ec_sync_info_t`, `ec_pdo_entry_reg_t`.
* **Functions** (all prefixed `ecrt_`), used in two phases:
  * *Configuration (non-RT):* `ecrt_request_master()`, `ecrt_master_create_domain()`, `ecrt_master_slave_config()`, `ecrt_slave_config_pdos()`, `ecrt_domain_reg_pdo_entry_list()`, `ecrt_master_activate()`.
  * *Cyclic (RT loop):* `ecrt_master_receive()`, `ecrt_domain_process()`, then read/write process data, then `ecrt_domain_queue()` and `ecrt_master_send()`.
  * *Diagnostics:* `ecrt_master_state()`, `ecrt_domain_state()`, `ecrt_slave_config_state()`.
* **Process-data access macros:** `EC_READ_BIT`, `EC_WRITE_BIT`, `EC_READ_U16`, `EC_WRITE_S32`, etc. They read and write the domain memory (`ecrt_domain_data()`) at the offsets returned by PDO registration, and handle endianness.

**`ecrt.h` is neither generated nor device-specific.** It is a static source file from the IgH release (`~/sw-dev/ethercat-1.6.13/include/ecrt.h`), copied unchanged to `/usr/local/include` by `make install`, and identical for every slave type. It only depends on the master version: it defines version macros (`ECRT_VER_MAJOR`/`ECRT_VER_MINOR` = 1.6) and `EC_HAVE_...` feature flags. Everything device-specific lives in the application (`prt5144_test.c`): vendor ID and product code, the PDO entry, PDO and sync manager tables, and the `domain1_regs` registration list. Only the tables are generated, by `ethercat cstruct` from the slave's SII/EEPROM, and they go into the application code, not into `ecrt.h`.

The header only declares the API. The implementation for user space is **`libethercat.so`** (in `/usr/local/lib`). It forwards each call as an ioctl to the kernel master module through `/dev/EtherCAT0`. That's why a program needs both `-I` (header) and `-lethercat` (library) to build, and access to `/dev/EtherCAT0` to run (granted by the udev rule in section 3). Kernel-module applications use the same API directly, without the library.

Slave-specific structs, vendor ID and product code don't have to be typed in by hand:
```bash
ethercat cstruct -p 0    # sync/PDO structs; vendor ID + product code appear in the header comment
ethercat slaves -v       # same identity, plus more slave details
```
For the PRT5144: Vendor ID `0x000001a1`, Product code `0x00000032`, Revision `0x00001000`. `cstruct` prints the identity only as a comment because it only generates the PDO layout. The `ecrt_master_slave_config()` call and the `ec_pdo_entry_reg_t` list that need the identity are written in the application.

### 5.2 Compile
```bash
gcc -O2 -Wall prt5144_test.c -o prt5144_test -I/usr/local/include -L/usr/local/lib -lethercat -lpthread -lrt
```
* Plain `gcc prt5144_test.c` compiles but **fails to link** with `undefined reference to 'ecrt_...'`. `-lethercat` is required.
* If the program can't start because of `libethercat.so.1: cannot open shared object file`, re-run `sudo ldconfig` (needs `/etc/ld.so.conf.d/ethercat.conf` from section 3).

### 5.3 Run
The EtherCAT service must be running (`ethercat master` shows `Link: UP`, and `ethercat slaves` lists the slave in `PREOP`).

```bash
sudo ./prt5144_test
```
Alternative without running the whole program as root: grant only the two RT capabilities (`/dev/EtherCAT0` is already `0666`):
```bash
sudo setcap cap_sys_nice,cap_ipc_lock+ep ./prt5144_test
./prt5144_test
```
* `CAP_IPC_LOCK` → `mlockall()`; `CAP_SYS_NICE` → `SCHED_FIFO` priority 80.
* `setcap` must be repeated after every recompile (the new binary loses the capability).

**Expected behaviour:**
* Within about 1 s: `PRT5144: operational` and `Domain1: State 2 (complete)`. While the program runs, `ethercat slaves` in a second terminal shows the slave in `OP`.
* `OUT0`–`OUT7` light up one at a time, advancing every 500 ms.
* Every change on `IN0`–`IN7` prints an `Inputs changed: ...` line. Inputs are only evaluated while the working counter is complete.
* `Ctrl+C` → outputs are driven to 0 for 10 cycles, then the master is released → `Stopped cleanly.` The slave returns to `PREOP`.

### 5.4 Test Result (2026-11-16)
Ran `./prt5144_test` on the T8PLUS. **Confirmed:** digital outputs `OUT0`–`OUT7` switch on one after another in a cycle, as expected. The master, the slave config and the PDO mapping work end-to-end, and the slave reaches `OP` with outputs being exchanged.

*Still to check:* the console messages (`PRT5144: operational`, `Domain1: State 2 (complete)`), input changes on `IN0`–`IN7`, and outputs going to 0 after `Ctrl+C`.

**Troubleshooting:** If the slave stays at AL state `0x02` (PREOP) or the domain state stays `0 (zero)`, check `dmesg | grep -i ethercat` for the master's error messages (e.g. vendor ID/product code mismatch).
