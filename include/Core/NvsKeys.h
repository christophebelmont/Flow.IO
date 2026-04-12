#pragma once
/**
 * @file NvsKeys.h
 * @brief Centralized NVS key constants used by ConfigStore-registered variables.
 */

namespace NvsKeys {

/** @brief Preferences namespace opened at boot (`main.cpp`). */
constexpr char StorageNamespace[] = "flowio"; // Preferences namespace name used at boot to open the firmware NVS partition.
/** @brief Config schema version key read/written by `ConfigStore::runMigrations`. */
constexpr char ConfigVersion[] = "cfg_ver"; // Persistent schema-version marker used to select and run config migrations.

namespace Wifi {
constexpr char Enabled[] = "wifi_en"; // WiFi module persisted key for field `wifi_en`.
constexpr char Ssid[] = "wifi_ssid"; // WiFi module persisted key for field `wifi_ssid`.
constexpr char Pass[] = "wifi_pass"; // WiFi module persisted key for field `wifi_pass`.
constexpr char Mdns[] = "wifi_mdns"; // WiFi module persisted key for field `wifi_mdns`.
}  // namespace Wifi

namespace Mqtt {
constexpr char Host[] = "mq_host"; // MQTT module persisted key for field `mq_host`.
constexpr char Port[] = "mq_port"; // MQTT module persisted key for field `mq_port`.
constexpr char User[] = "mq_user"; // MQTT module persisted key for field `mq_user`.
constexpr char Pass[] = "mq_pass"; // MQTT module persisted key for field `mq_pass`.
constexpr char BaseTopic[] = "mq_base"; // MQTT module persisted key for field `mq_base`.
constexpr char TopicDeviceId[] = "mq_tid"; // MQTT topic device id override for `<base>/<deviceId>/...`.
constexpr char Enabled[] = "mq_en"; // MQTT module persisted key for field `mq_en`.
constexpr char SensorMinPublishMs[] = "mq_smin"; // MQTT module persisted key for field `mq_smin`.
}  // namespace Mqtt

namespace Ha {
constexpr char Enabled[] = "ha_en"; // Home Assistant module persisted key for field `ha_en`.
constexpr char Vendor[] = "ha_vend"; // Home Assistant module persisted key for field `ha_vend`.
constexpr char DeviceId[] = "ha_devid"; // Home Assistant module persisted key for field `ha_devid`.
constexpr char DiscoveryPrefix[] = "ha_pref"; // Home Assistant module persisted key for field `ha_pref`.
constexpr char Model[] = "ha_model"; // Home Assistant module persisted key for field `ha_model`.
}  // namespace Ha

namespace Time {
constexpr char Server1[] = "ntp_s1"; // Time module persisted key for field `ntp_s1`.
constexpr char Server2[] = "ntp_s2"; // Time module persisted key for field `ntp_s2`.
constexpr char Tz[] = "ntp_tz"; // Time module persisted key for field `ntp_tz`.
constexpr char Enabled[] = "ntp_en"; // Time module persisted key for field `ntp_en`.
constexpr char WeekStartMonday[] = "tm_wkmon"; // Time module persisted key for field `tm_wkmon`.
constexpr char ScheduleBlob[] = "tm_sched"; // Time module persisted key for field `tm_sched`.
}  // namespace Time

namespace SystemMonitor {
constexpr char TraceEnabled[] = "sm_tren"; // System monitor module persisted key for field `sm_tren`.
constexpr char TracePeriodMs[] = "sm_trms"; // System monitor module persisted key for field `sm_trms`.
}  // namespace SystemMonitor

namespace Io {
constexpr char IO_A00[] = "io_a000"; // IO module persisted key for field `io_a000`.
constexpr char IO_A01[] = "io_a001"; // IO module persisted key for field `io_a001`.
constexpr char IO_A0C[] = "io_a00c"; // IO module persisted key for field `io_a00c`.
constexpr char IO_A0BP[] = "io_a00bp"; // IO module persisted key for field `io_a00bp`.
constexpr char IO_A0N[] = "io_a00n"; // IO module persisted key for field `io_a00n`.
constexpr char IO_A0NM[] = "io_a00nm"; // IO module persisted key for field `io_a00nm`.
constexpr char IO_A0P[] = "io_a00p"; // IO module persisted key for field `io_a00p`.
constexpr char IO_A0S[] = "io_a00s"; // IO module persisted key for field `io_a00s`.
constexpr char IO_A0X[] = "io_a00x"; // IO module persisted key for field `io_a00x`.
constexpr char IO_A10[] = "io_a010"; // IO module persisted key for field `io_a010`.
constexpr char IO_A11[] = "io_a011"; // IO module persisted key for field `io_a011`.
constexpr char IO_A1C[] = "io_a01c"; // IO module persisted key for field `io_a01c`.
constexpr char IO_A1BP[] = "io_a01bp"; // IO module persisted key for field `io_a01bp`.
constexpr char IO_A1N[] = "io_a01n"; // IO module persisted key for field `io_a01n`.
constexpr char IO_A1NM[] = "io_a01nm"; // IO module persisted key for field `io_a01nm`.
constexpr char IO_A1P[] = "io_a01p"; // IO module persisted key for field `io_a01p`.
constexpr char IO_A1S[] = "io_a01s"; // IO module persisted key for field `io_a01s`.
constexpr char IO_A1X[] = "io_a01x"; // IO module persisted key for field `io_a01x`.
constexpr char IO_A20[] = "io_a020"; // IO module persisted key for field `io_a020`.
constexpr char IO_A21[] = "io_a021"; // IO module persisted key for field `io_a021`.
constexpr char IO_A2C[] = "io_a02c"; // IO module persisted key for field `io_a02c`.
constexpr char IO_A2BP[] = "io_a02bp"; // IO module persisted key for field `io_a02bp`.
constexpr char IO_A2N[] = "io_a02n"; // IO module persisted key for field `io_a02n`.
constexpr char IO_A2NM[] = "io_a02nm"; // IO module persisted key for field `io_a02nm`.
constexpr char IO_A2P[] = "io_a02p"; // IO module persisted key for field `io_a02p`.
constexpr char IO_A2S[] = "io_a02s"; // IO module persisted key for field `io_a02s`.
constexpr char IO_A2X[] = "io_a02x"; // IO module persisted key for field `io_a02x`.
constexpr char IO_A30[] = "io_a030"; // IO module persisted key for field `io_a030`.
constexpr char IO_A31[] = "io_a031"; // IO module persisted key for field `io_a031`.
constexpr char IO_A3C[] = "io_a03c"; // IO module persisted key for field `io_a03c`.
constexpr char IO_A3BP[] = "io_a03bp"; // IO module persisted key for field `io_a03bp`.
constexpr char IO_A3N[] = "io_a03n"; // IO module persisted key for field `io_a03n`.
constexpr char IO_A3NM[] = "io_a03nm"; // IO module persisted key for field `io_a03nm`.
constexpr char IO_A3P[] = "io_a03p"; // IO module persisted key for field `io_a03p`.
constexpr char IO_A3S[] = "io_a03s"; // IO module persisted key for field `io_a03s`.
constexpr char IO_A3X[] = "io_a03x"; // IO module persisted key for field `io_a03x`.
constexpr char IO_A40[] = "io_a040"; // IO module persisted key for field `io_a040`.
constexpr char IO_A41[] = "io_a041"; // IO module persisted key for field `io_a041`.
constexpr char IO_A4C[] = "io_a04c"; // IO module persisted key for field `io_a04c`.
constexpr char IO_A4BP[] = "io_a04bp"; // IO module persisted key for field `io_a04bp`.
constexpr char IO_A4N[] = "io_a04n"; // IO module persisted key for field `io_a04n`.
constexpr char IO_A4NM[] = "io_a04nm"; // IO module persisted key for field `io_a04nm`.
constexpr char IO_A4P[] = "io_a04p"; // IO module persisted key for field `io_a04p`.
constexpr char IO_A4S[] = "io_a04s"; // IO module persisted key for field `io_a04s`.
constexpr char IO_A4X[] = "io_a04x"; // IO module persisted key for field `io_a04x`.
constexpr char IO_A50[] = "io_a050"; // IO module persisted key for field `io_a050`.
constexpr char IO_A51[] = "io_a051"; // IO module persisted key for field `io_a051`.
constexpr char IO_A5C[] = "io_a05c"; // IO module persisted key for field `io_a05c`.
constexpr char IO_A5BP[] = "io_a05bp"; // IO module persisted key for field `io_a05bp`.
constexpr char IO_A5N[] = "io_a05n"; // IO module persisted key for field `io_a05n`.
constexpr char IO_A5NM[] = "io_a05nm"; // IO module persisted key for field `io_a05nm`.
constexpr char IO_A5P[] = "io_a05p"; // IO module persisted key for field `io_a05p`.
constexpr char IO_A5S[] = "io_a05s"; // IO module persisted key for field `io_a05s`.
constexpr char IO_A5X[] = "io_a05x"; // IO module persisted key for field `io_a05x`.
#define FLOW_IO_ANALOG_NVS_KEYS(SLOT_STR, KEYNM, KEYBP, KEYC0, KEYC1, KEYP) \
constexpr char KEYC0[] = "io_a" SLOT_STR "0"; /* IO module persisted key for field `io_a" SLOT_STR "0`. */ \
constexpr char KEYC1[] = "io_a" SLOT_STR "1"; /* IO module persisted key for field `io_a" SLOT_STR "1`. */ \
constexpr char KEYBP[] = "io_a" SLOT_STR "bp"; /* IO module persisted key for field `io_a" SLOT_STR "bp`. */ \
constexpr char KEYNM[] = "io_a" SLOT_STR "nm"; /* IO module persisted key for field `io_a" SLOT_STR "nm`. */ \
constexpr char KEYP[] = "io_a" SLOT_STR "p"; /* IO module persisted key for field `io_a" SLOT_STR "p`. */
FLOW_IO_ANALOG_NVS_KEYS("06", IO_A6NM, IO_A6BP, IO_A60, IO_A61, IO_A6P)
FLOW_IO_ANALOG_NVS_KEYS("07", IO_A7NM, IO_A7BP, IO_A70, IO_A71, IO_A7P)
FLOW_IO_ANALOG_NVS_KEYS("08", IO_A8NM, IO_A8BP, IO_A80, IO_A81, IO_A8P)
FLOW_IO_ANALOG_NVS_KEYS("09", IO_A9NM, IO_A9BP, IO_A90, IO_A91, IO_A9P)
FLOW_IO_ANALOG_NVS_KEYS("10", IO_A10NM, IO_A10BP, IO_A100, IO_A101, IO_A10P)
FLOW_IO_ANALOG_NVS_KEYS("11", IO_A11NM, IO_A11BP, IO_A110, IO_A111, IO_A11P)
FLOW_IO_ANALOG_NVS_KEYS("12", IO_A12NM, IO_A12BP, IO_A120, IO_A121, IO_A12P)
FLOW_IO_ANALOG_NVS_KEYS("13", IO_A13NM, IO_A13BP, IO_A130, IO_A131, IO_A13P)
FLOW_IO_ANALOG_NVS_KEYS("14", IO_A14NM, IO_A14BP, IO_A140, IO_A141, IO_A14P)
#undef FLOW_IO_ANALOG_NVS_KEYS
constexpr char IO_ADS[] = "io_ads"; // IO module persisted key for field `io_ads`.
constexpr char IO_AEAD[] = "io_aead"; // IO module persisted key for field `io_aead`.
constexpr char IO_AGAI[] = "io_agai"; // IO module persisted key for field `io_agai`.
constexpr char IO_AIAD[] = "io_aiad"; // IO module persisted key for field `io_aiad`.
constexpr char IO_ARAT[] = "io_arat"; // IO module persisted key for field `io_arat`.
constexpr char IO_I0AH[] = "io_i00ah"; // IO module persisted key for field `io_i00ah`.
constexpr char IO_I0BP[] = "io_i00bp"; // IO module persisted key for field `io_i00bp`.
constexpr char IO_I0C0[] = "io_i00c0"; // IO module persisted key for field `io_i00c0`.
constexpr char IO_I0CT[] = "io_i00ct"; // IO module persisted key for field `io_i00ct`.
constexpr char IO_I0ED[] = "io_i00ed"; // IO module persisted key for field `io_i00ed`.
constexpr char IO_I0MD[] = "io_i00md"; // IO module persisted key for field `io_i00md`.
constexpr char IO_I0NM[] = "io_i00nm"; // IO module persisted key for field `io_i00nm`.
constexpr char IO_I0P[] = "io_i00p"; // IO module persisted key for field `io_i00p`.
constexpr char IO_I0PN[] = "io_i00pn"; // IO module persisted key for field `io_i00pn`.
constexpr char IO_I0PU[] = "io_i00pu"; // IO module persisted key for field `io_i00pu`.
constexpr char IO_I1AH[] = "io_i01ah"; // IO module persisted key for field `io_i01ah`.
constexpr char IO_I1BP[] = "io_i01bp"; // IO module persisted key for field `io_i01bp`.
constexpr char IO_I1C0[] = "io_i01c0"; // IO module persisted key for field `io_i01c0`.
constexpr char IO_I1CT[] = "io_i01ct"; // IO module persisted key for field `io_i01ct`.
constexpr char IO_I1ED[] = "io_i01ed"; // IO module persisted key for field `io_i01ed`.
constexpr char IO_I1MD[] = "io_i01md"; // IO module persisted key for field `io_i01md`.
constexpr char IO_I1NM[] = "io_i01nm"; // IO module persisted key for field `io_i01nm`.
constexpr char IO_I1P[] = "io_i01p"; // IO module persisted key for field `io_i01p`.
constexpr char IO_I1PN[] = "io_i01pn"; // IO module persisted key for field `io_i01pn`.
constexpr char IO_I1PU[] = "io_i01pu"; // IO module persisted key for field `io_i01pu`.
constexpr char IO_I2AH[] = "io_i02ah"; // IO module persisted key for field `io_i02ah`.
constexpr char IO_I2BP[] = "io_i02bp"; // IO module persisted key for field `io_i02bp`.
constexpr char IO_I2C0[] = "io_i02c0"; // IO module persisted key for field `io_i02c0`.
constexpr char IO_I2CT[] = "io_i02ct"; // IO module persisted key for field `io_i02ct`.
constexpr char IO_I2ED[] = "io_i02ed"; // IO module persisted key for field `io_i02ed`.
constexpr char IO_I2MD[] = "io_i02md"; // IO module persisted key for field `io_i02md`.
constexpr char IO_I2NM[] = "io_i02nm"; // IO module persisted key for field `io_i02nm`.
constexpr char IO_I2P[] = "io_i02p"; // IO module persisted key for field `io_i02p`.
constexpr char IO_I2PN[] = "io_i02pn"; // IO module persisted key for field `io_i02pn`.
constexpr char IO_I2PU[] = "io_i02pu"; // IO module persisted key for field `io_i02pu`.
constexpr char IO_I3AH[] = "io_i03ah"; // IO module persisted key for field `io_i03ah`.
constexpr char IO_I3BP[] = "io_i03bp"; // IO module persisted key for field `io_i03bp`.
constexpr char IO_I3C0[] = "io_i03c0"; // IO module persisted key for field `io_i03c0`.
constexpr char IO_I3CT[] = "io_i03ct"; // IO module persisted key for field `io_i03ct`.
constexpr char IO_I3ED[] = "io_i03ed"; // IO module persisted key for field `io_i03ed`.
constexpr char IO_I3MD[] = "io_i03md"; // IO module persisted key for field `io_i03md`.
constexpr char IO_I3NM[] = "io_i03nm"; // IO module persisted key for field `io_i03nm`.
constexpr char IO_I3P[] = "io_i03p"; // IO module persisted key for field `io_i03p`.
constexpr char IO_I3PN[] = "io_i03pn"; // IO module persisted key for field `io_i03pn`.
constexpr char IO_I3PU[] = "io_i03pu"; // IO module persisted key for field `io_i03pu`.
constexpr char IO_I4AH[] = "io_i04ah"; // IO module persisted key for field `io_i04ah`.
constexpr char IO_I4BP[] = "io_i04bp"; // IO module persisted key for field `io_i04bp`.
constexpr char IO_I4C0[] = "io_i04c0"; // IO module persisted key for field `io_i04c0`.
constexpr char IO_I4CT[] = "io_i04ct"; // IO module persisted key for field `io_i04ct`.
constexpr char IO_I4ED[] = "io_i04ed"; // IO module persisted key for field `io_i04ed`.
constexpr char IO_I4MD[] = "io_i04md"; // IO module persisted key for field `io_i04md`.
constexpr char IO_I4NM[] = "io_i04nm"; // IO module persisted key for field `io_i04nm`.
constexpr char IO_I4P[] = "io_i04p"; // IO module persisted key for field `io_i04p`.
constexpr char IO_I4PN[] = "io_i04pn"; // IO module persisted key for field `io_i04pn`.
constexpr char IO_I4PU[] = "io_i04pu"; // IO module persisted key for field `io_i04pu`.
constexpr char IO_D0AH[] = "io_d00ah"; // IO module persisted key for field `io_d00ah`.
constexpr char IO_D0BP[] = "io_d00bp"; // IO module persisted key for field `io_d00bp`.
constexpr char IO_D0IN[] = "io_d00in"; // IO module persisted key for field `io_d00in`.
constexpr char IO_D0MO[] = "io_d00mo"; // IO module persisted key for field `io_d00mo`.
constexpr char IO_D0NM[] = "io_d00nm"; // IO module persisted key for field `io_d00nm`.
constexpr char IO_D0PM[] = "io_d00pm"; // IO module persisted key for field `io_d00pm`.
constexpr char IO_D0PN[] = "io_d00pn"; // IO module persisted key for field `io_d00pn`.
constexpr char IO_D1AH[] = "io_d01ah"; // IO module persisted key for field `io_d01ah`.
constexpr char IO_D1BP[] = "io_d01bp"; // IO module persisted key for field `io_d01bp`.
constexpr char IO_D1IN[] = "io_d01in"; // IO module persisted key for field `io_d01in`.
constexpr char IO_D1MO[] = "io_d01mo"; // IO module persisted key for field `io_d01mo`.
constexpr char IO_D1NM[] = "io_d01nm"; // IO module persisted key for field `io_d01nm`.
constexpr char IO_D1PM[] = "io_d01pm"; // IO module persisted key for field `io_d01pm`.
constexpr char IO_D1PN[] = "io_d01pn"; // IO module persisted key for field `io_d01pn`.
constexpr char IO_D2AH[] = "io_d02ah"; // IO module persisted key for field `io_d02ah`.
constexpr char IO_D2BP[] = "io_d02bp"; // IO module persisted key for field `io_d02bp`.
constexpr char IO_D2IN[] = "io_d02in"; // IO module persisted key for field `io_d02in`.
constexpr char IO_D2MO[] = "io_d02mo"; // IO module persisted key for field `io_d02mo`.
constexpr char IO_D2NM[] = "io_d02nm"; // IO module persisted key for field `io_d02nm`.
constexpr char IO_D2PM[] = "io_d02pm"; // IO module persisted key for field `io_d02pm`.
constexpr char IO_D2PN[] = "io_d02pn"; // IO module persisted key for field `io_d02pn`.
constexpr char IO_D3AH[] = "io_d03ah"; // IO module persisted key for field `io_d03ah`.
constexpr char IO_D3BP[] = "io_d03bp"; // IO module persisted key for field `io_d03bp`.
constexpr char IO_D3IN[] = "io_d03in"; // IO module persisted key for field `io_d03in`.
constexpr char IO_D3MO[] = "io_d03mo"; // IO module persisted key for field `io_d03mo`.
constexpr char IO_D3NM[] = "io_d03nm"; // IO module persisted key for field `io_d03nm`.
constexpr char IO_D3PM[] = "io_d03pm"; // IO module persisted key for field `io_d03pm`.
constexpr char IO_D3PN[] = "io_d03pn"; // IO module persisted key for field `io_d03pn`.
constexpr char IO_D4AH[] = "io_d04ah"; // IO module persisted key for field `io_d04ah`.
constexpr char IO_D4BP[] = "io_d04bp"; // IO module persisted key for field `io_d04bp`.
constexpr char IO_D4IN[] = "io_d04in"; // IO module persisted key for field `io_d04in`.
constexpr char IO_D4MO[] = "io_d04mo"; // IO module persisted key for field `io_d04mo`.
constexpr char IO_D4NM[] = "io_d04nm"; // IO module persisted key for field `io_d04nm`.
constexpr char IO_D4PM[] = "io_d04pm"; // IO module persisted key for field `io_d04pm`.
constexpr char IO_D4PN[] = "io_d04pn"; // IO module persisted key for field `io_d04pn`.
constexpr char IO_D5AH[] = "io_d05ah"; // IO module persisted key for field `io_d05ah`.
constexpr char IO_D5BP[] = "io_d05bp"; // IO module persisted key for field `io_d05bp`.
constexpr char IO_D5IN[] = "io_d05in"; // IO module persisted key for field `io_d05in`.
constexpr char IO_D5MO[] = "io_d05mo"; // IO module persisted key for field `io_d05mo`.
constexpr char IO_D5NM[] = "io_d05nm"; // IO module persisted key for field `io_d05nm`.
constexpr char IO_D5PM[] = "io_d05pm"; // IO module persisted key for field `io_d05pm`.
constexpr char IO_D5PN[] = "io_d05pn"; // IO module persisted key for field `io_d05pn`.
constexpr char IO_D6AH[] = "io_d06ah"; // IO module persisted key for field `io_d06ah`.
constexpr char IO_D6BP[] = "io_d06bp"; // IO module persisted key for field `io_d06bp`.
constexpr char IO_D6IN[] = "io_d06in"; // IO module persisted key for field `io_d06in`.
constexpr char IO_D6MO[] = "io_d06mo"; // IO module persisted key for field `io_d06mo`.
constexpr char IO_D6NM[] = "io_d06nm"; // IO module persisted key for field `io_d06nm`.
constexpr char IO_D6PM[] = "io_d06pm"; // IO module persisted key for field `io_d06pm`.
constexpr char IO_D6PN[] = "io_d06pn"; // IO module persisted key for field `io_d06pn`.
constexpr char IO_D7AH[] = "io_d07ah"; // IO module persisted key for field `io_d07ah`.
constexpr char IO_D7BP[] = "io_d07bp"; // IO module persisted key for field `io_d07bp`.
constexpr char IO_D7IN[] = "io_d07in"; // IO module persisted key for field `io_d07in`.
constexpr char IO_D7MO[] = "io_d07mo"; // IO module persisted key for field `io_d07mo`.
constexpr char IO_D7NM[] = "io_d07nm"; // IO module persisted key for field `io_d07nm`.
constexpr char IO_D7PM[] = "io_d07pm"; // IO module persisted key for field `io_d07pm`.
constexpr char IO_D7PN[] = "io_d07pn"; // IO module persisted key for field `io_d07pn`.
constexpr char IO_DIN[] = "io_din"; // IO module persisted key for field `io_din`.
constexpr char IO_DS[] = "io_ds"; // IO module persisted key for field `io_ds`.
constexpr char IO_EN[] = "io_en"; // IO module persisted key for field `io_en`.
constexpr char IO_SHTEN[] = "io_shten"; // IO module persisted key for field `io_sht40_enabled`.
constexpr char IO_SHTAD[] = "io_shtad"; // IO module persisted key for field `io_sht40_address`.
constexpr char IO_SHTPL[] = "io_shtpl"; // IO module persisted key for field `io_sht40_poll_ms`.
constexpr char IO_BMPEN[] = "io_bmpen"; // IO module persisted key for field `io_bmp280_enabled`.
constexpr char IO_BMPAD[] = "io_bmpad"; // IO module persisted key for field `io_bmp280_address`.
constexpr char IO_BMPPL[] = "io_bmppl"; // IO module persisted key for field `io_bmp280_poll_ms`.
constexpr char IO_BMEEN[] = "io_bmeen"; // IO module persisted key for field `io_bme680_enabled`.
constexpr char IO_BMEAD[] = "io_bmead"; // IO module persisted key for field `io_bme680_address`.
constexpr char IO_BMEPL[] = "io_bmepl"; // IO module persisted key for field `io_bme680_poll_ms`.
constexpr char IO_INAEN[] = "io_inaen"; // IO module persisted key for field `io_ina226_enabled`.
constexpr char IO_INAAD[] = "io_inaad"; // IO module persisted key for field `io_ina226_address`.
constexpr char IO_INAPL[] = "io_inapl"; // IO module persisted key for field `io_ina226_poll_ms`.
constexpr char IO_INASH[] = "io_inash"; // IO module persisted key for field `io_ina226_shunt_ohms`.
constexpr char IO_PCFAD[] = "io_pcfad"; // IO module persisted key for field `io_pcfad`.
constexpr char IO_PCFAL[] = "io_pcfal"; // IO module persisted key for field `io_pcfal`.
constexpr char IO_PCFEN[] = "io_pcfen"; // IO module persisted key for field `io_pcfen`.
constexpr char IO_PCFMK[] = "io_pcfmk"; // IO module persisted key for field `io_pcfmk`.
constexpr char IO_SCL[] = "io_scl"; // IO module persisted key for field `io_scl`.
constexpr char IO_SDA[] = "io_sda"; // IO module persisted key for field `io_sda`.
constexpr char IO_TREN[] = "io_tren"; // IO module persisted key for field `io_tren`.
constexpr char IO_TRMS[] = "io_trms"; // IO module persisted key for field `io_trms`.
constexpr char DsRomWater[] = "io_dswrm"; // IO module runtime DS18 water ROM blob.
constexpr char DsRomAir[] = "io_dsarm"; // IO module runtime DS18 air ROM blob.
}  // namespace Io

namespace I2cCfg {
constexpr char ClientEnabled[] = "ic_cli_en"; // I2C cfg client enabled.
constexpr char ClientSda[] = "ic_cli_sda"; // I2C cfg client SDA pin.
constexpr char ClientScl[] = "ic_cli_scl"; // I2C cfg client SCL pin.
constexpr char ClientFreq[] = "ic_cli_frq"; // I2C cfg client bus frequency.
constexpr char ClientAddr[] = "ic_cli_adr"; // I2C cfg client target slave address.
constexpr char DashboardEnabledFmt[] = "ic_d%u_en"; // I2C cfg dashboard slot enabled.
constexpr char DashboardRuntimeIdFmt[] = "ic_d%u_rt"; // I2C cfg dashboard slot runtime UI id.
constexpr char DashboardLabelFmt[] = "ic_d%u_lb"; // I2C cfg dashboard slot label.
constexpr char DashboardColorIdFmt[] = "ic_d%u_ci"; // I2C cfg dashboard slot color preset id.

constexpr char ServerEnabled[] = "ic_srv_en"; // I2C cfg server enabled.
constexpr char ServerSda[] = "ic_srv_sda"; // I2C cfg server SDA pin.
constexpr char ServerScl[] = "ic_srv_scl"; // I2C cfg server SCL pin.
constexpr char ServerFreq[] = "ic_srv_frq"; // I2C cfg server bus frequency.
constexpr char ServerAddr[] = "ic_srv_adr"; // I2C cfg server own slave address.
}  // namespace I2cCfg

namespace PoolLogic {
constexpr char Enabled[] = "pl_en"; // Pool logic module persisted key for field `pl_en`.
constexpr char AutoMode[] = "pl_auto"; // Pool logic module persisted key for field `pl_auto`.
constexpr char WinterMode[] = "pl_wint"; // Pool logic module persisted key for field `pl_wint`.
constexpr char PhAutoMode[] = "pl_pha"; // Pool logic module persisted key for field `pl_pha`.
constexpr char OrpAutoMode[] = "pl_orpa"; // Pool logic module persisted key for field `pl_orpa`.
constexpr char PhDosePlus[] = "pl_phpl"; // Pool logic module persisted key for field `pl_phpl`.
constexpr char ElectrolyseMode[] = "pl_elec"; // Pool logic module persisted key for field `pl_elec`.
constexpr char ElectroRunMode[] = "pl_erun"; // Pool logic module persisted key for field `pl_erun`.
constexpr char TempLow[] = "pl_tlow"; // Pool logic module persisted key for field `pl_tlow`.
constexpr char TempSetpoint[] = "pl_tset"; // Pool logic module persisted key for field `pl_tset`.
constexpr char FiltrationStartMin[] = "pl_smin"; // Pool logic module persisted key for field `pl_smin`.
constexpr char FiltrationStopMax[] = "pl_smax"; // Pool logic module persisted key for field `pl_smax`.
constexpr char PhIoId[] = "pl_phiid"; // Pool logic module persisted key for field `pl_phiid`.
constexpr char OrpIoId[] = "pl_oiid"; // Pool logic module persisted key for field `pl_oiid`.
constexpr char PsiIoId[] = "pl_piid"; // Pool logic module persisted key for field `pl_piid`.
constexpr char WaterTempIoId[] = "pl_wiid"; // Pool logic module persisted key for field `pl_wiid`.
constexpr char AirTempIoId[] = "pl_aiid"; // Pool logic module persisted key for field `pl_aiid`.
constexpr char LevelIoId[] = "pl_liid"; // Pool logic module persisted key for field `pl_liid`.
constexpr char PhLevelIoId[] = "pl_phli"; // Pool logic module persisted key for field `pl_phli`.
constexpr char ChlorineLevelIoId[] = "pl_clli"; // Pool logic module persisted key for field `pl_clli`.
constexpr char PsiLow[] = "pl_psil"; // Pool logic module persisted key for field `pl_psil`.
constexpr char PsiHigh[] = "pl_psih"; // Pool logic module persisted key for field `pl_psih`.
constexpr char WinterStart[] = "pl_wstr"; // Pool logic module persisted key for field `pl_wstr`.
constexpr char FreezeHold[] = "pl_whld"; // Pool logic module persisted key for field `pl_whld`.
constexpr char SecureElectro[] = "pl_sect"; // Pool logic module persisted key for field `pl_sect`.
constexpr char PhSetpoint[] = "pl_phsp"; // Pool logic module persisted key for field `pl_phsp`.
constexpr char OrpSetpoint[] = "pl_orps"; // Pool logic module persisted key for field `pl_orps`.
constexpr char PhKp[] = "pl_phkp"; // Pool logic module persisted key for field `pl_phkp`.
constexpr char PhKi[] = "pl_phki"; // Pool logic module persisted key for field `pl_phki`.
constexpr char PhKd[] = "pl_phkd"; // Pool logic module persisted key for field `pl_phkd`.
constexpr char OrpKp[] = "pl_okp"; // Pool logic module persisted key for field `pl_okp`.
constexpr char OrpKi[] = "pl_oki"; // Pool logic module persisted key for field `pl_oki`.
constexpr char OrpKd[] = "pl_okd"; // Pool logic module persisted key for field `pl_okd`.
constexpr char PhWindowMs[] = "pl_phwms"; // Pool logic module persisted key for field `pl_phwms`.
constexpr char OrpWindowMs[] = "pl_owms"; // Pool logic module persisted key for field `pl_owms`.
constexpr char PidMinOnMs[] = "pl_pmon"; // Pool logic module persisted key for field `pl_pmon`.
constexpr char PidSampleMs[] = "pl_psamp"; // Pool logic module persisted key for field `pl_psamp`.
constexpr char PsiDelay[] = "pl_psdt"; // Pool logic module persisted key for field `pl_psdt`.
constexpr char DelayPids[] = "pl_dpds"; // Pool logic module persisted key for field `pl_dpds`.
constexpr char DelayElectro[] = "pl_delt"; // Pool logic module persisted key for field `pl_delt`.
constexpr char RobotDelay[] = "pl_rdel"; // Pool logic module persisted key for field `pl_rdel`.
constexpr char RobotDuration[] = "pl_rdur"; // Pool logic module persisted key for field `pl_rdur`.
constexpr char FillingMinOn[] = "pl_fmin"; // Pool logic module persisted key for field `pl_fmin`.
constexpr char FiltrationSlot[] = "pl_sfil"; // Pool logic module persisted key for field `pl_sfil`.
constexpr char SwgSlot[] = "pl_sswg"; // Pool logic module persisted key for field `pl_sswg`.
constexpr char RobotSlot[] = "pl_srob"; // Pool logic module persisted key for field `pl_srob`.
constexpr char FillingSlot[] = "pl_sfill"; // Pool logic module persisted key for field `pl_sfill`.
constexpr char PhPumpSlot[] = "pl_sphp"; // Pool logic module persisted key for field `pl_sphp`.
constexpr char OrpPumpSlot[] = "pl_sorp"; // Pool logic module persisted key for field `pl_sorp`.
constexpr char FiltrationCalcStart[] = "pl_fcst"; // Pool logic runtime key for calculated filtration start hour.
constexpr char FiltrationCalcStop[] = "pl_fcen"; // Pool logic runtime key for calculated filtration stop hour.
}  // namespace PoolLogic

namespace PoolDevice {
/** @brief printf format for per-slot `enabled` key (example `pd0en`). */
constexpr char EnabledFmt[] = "pd%uen"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot dependency mask key (example `pd0dp`). */
constexpr char DependsFmt[] = "pd%udp"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot flow key (example `pd0flh`). */
constexpr char FlowFmt[] = "pd%uflh"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot tank capacity key (example `pd0tc`). */
constexpr char TankCapFmt[] = "pd%utc"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot tank initial value key (example `pd0ti`). */
constexpr char TankInitFmt[] = "pd%uti"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot max daily uptime in seconds (example `pd0mu`). */
constexpr char MaxUptimeFmt[] = "pd%umu"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot runtime metrics blob key (example `pd0rt`). */
constexpr char RuntimeFmt[] = "pd%urt"; // Pool device module runtime metrics key template; `%u` is replaced by slot index before NVS access.
}  // namespace PoolDevice

namespace Alarm {
constexpr char Enabled[] = "al_en"; // Alarm module persisted key for field `enabled`.
constexpr char EvalPeriodMs[] = "al_epms"; // Alarm module persisted key for field `eval_period_ms`.
}  // namespace Alarm

namespace Hmi {
constexpr char LedsEnabled[] = "hmi_leds"; // HMI module persisted key for logical LED-panel writes enable.
constexpr char NextionEnabled[] = "hmi_nxen"; // HMI module persisted key for Nextion output enable.
constexpr char VeniceEnabled[] = "hmi_vcen"; // HMI module persisted key for Venice RF433 output enable.
constexpr char VeniceTxGpio[] = "hmi_vcgp"; // HMI module persisted key for Venice RF433 TX GPIO.
}  // namespace Hmi

}  // namespace NvsKeys
