/* Master 0, Slave 0, "PRT.5144"
 * Vendor ID:       0x000001a1
 * Product code:    0x00000032
 * Revision number: 0x00001000
 */

ec_pdo_entry_info_t slave_0_pdo_entries[] = {
    {0x7000, 0x01, 1}, /* Output */
    {0x7010, 0x01, 1}, /* Output */
    {0x7020, 0x01, 1}, /* Output */
    {0x7030, 0x01, 1}, /* Output */
    {0x7040, 0x01, 1}, /* Output */
    {0x7050, 0x01, 1}, /* Output */
    {0x7060, 0x01, 1}, /* Output */
    {0x7070, 0x01, 1}, /* Output */
    {0x6000, 0x01, 1}, /* Input */
    {0x6010, 0x01, 1}, /* Input */
    {0x6020, 0x01, 1}, /* Input */
    {0x6030, 0x01, 1}, /* Input */
    {0x6040, 0x01, 1}, /* Input */
    {0x6050, 0x01, 1}, /* Input */
    {0x6060, 0x01, 1}, /* Input */
    {0x6070, 0x01, 1}, /* Input */
};

ec_pdo_info_t slave_0_pdos[] = {
    {0x1600, 1, slave_0_pdo_entries + 0}, /* OUT0 */
    {0x1601, 1, slave_0_pdo_entries + 1}, /* OUT1 */
    {0x1602, 1, slave_0_pdo_entries + 2}, /* OUT2 */
    {0x1603, 1, slave_0_pdo_entries + 3}, /* OUT3 */
    {0x1604, 1, slave_0_pdo_entries + 4}, /* OUT4 */
    {0x1605, 1, slave_0_pdo_entries + 5}, /* OUT5 */
    {0x1606, 1, slave_0_pdo_entries + 6}, /* OUT6 */
    {0x1607, 1, slave_0_pdo_entries + 7}, /* OUT7 */
    {0x1a00, 1, slave_0_pdo_entries + 8}, /* IN0 */
    {0x1a01, 1, slave_0_pdo_entries + 9}, /* IN1 */
    {0x1a02, 1, slave_0_pdo_entries + 10}, /* IN2 */
    {0x1a03, 1, slave_0_pdo_entries + 11}, /* IN3 */
    {0x1a04, 1, slave_0_pdo_entries + 12}, /* IN4 */
    {0x1a05, 1, slave_0_pdo_entries + 13}, /* IN5 */
    {0x1a06, 1, slave_0_pdo_entries + 14}, /* IN6 */
    {0x1a07, 1, slave_0_pdo_entries + 15}, /* IN7 */
};

ec_sync_info_t slave_0_syncs[] = {
    {0, EC_DIR_OUTPUT, 8, slave_0_pdos + 0, EC_WD_ENABLE},
    {1, EC_DIR_INPUT, 8, slave_0_pdos + 8, EC_WD_DISABLE},
    {0xff}
};
