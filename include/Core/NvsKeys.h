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
constexpr char IO_A00[] = "io_a00"; // IO module persisted key for field `io_a00`.
constexpr char IO_A01[] = "io_a01"; // IO module persisted key for field `io_a01`.
constexpr char IO_A0C[] = "io_a0c"; // IO module persisted key for field `io_a0c`.
constexpr char IO_A0N[] = "io_a0n"; // IO module persisted key for field `io_a0n`.
constexpr char IO_A0NM[] = "io_a0nm"; // IO module persisted key for field `io_a0nm`.
constexpr char IO_A0P[] = "io_a0p"; // IO module persisted key for field `io_a0p`.
constexpr char IO_A0S[] = "io_a0s"; // IO module persisted key for field `io_a0s`.
constexpr char IO_A0X[] = "io_a0x"; // IO module persisted key for field `io_a0x`.
constexpr char IO_A10[] = "io_a10"; // IO module persisted key for field `io_a10`.
constexpr char IO_A11[] = "io_a11"; // IO module persisted key for field `io_a11`.
constexpr char IO_A1C[] = "io_a1c"; // IO module persisted key for field `io_a1c`.
constexpr char IO_A1N[] = "io_a1n"; // IO module persisted key for field `io_a1n`.
constexpr char IO_A1NM[] = "io_a1nm"; // IO module persisted key for field `io_a1nm`.
constexpr char IO_A1P[] = "io_a1p"; // IO module persisted key for field `io_a1p`.
constexpr char IO_A1S[] = "io_a1s"; // IO module persisted key for field `io_a1s`.
constexpr char IO_A1X[] = "io_a1x"; // IO module persisted key for field `io_a1x`.
constexpr char IO_A20[] = "io_a20"; // IO module persisted key for field `io_a20`.
constexpr char IO_A21[] = "io_a21"; // IO module persisted key for field `io_a21`.
constexpr char IO_A2C[] = "io_a2c"; // IO module persisted key for field `io_a2c`.
constexpr char IO_A2N[] = "io_a2n"; // IO module persisted key for field `io_a2n`.
constexpr char IO_A2NM[] = "io_a2nm"; // IO module persisted key for field `io_a2nm`.
constexpr char IO_A2P[] = "io_a2p"; // IO module persisted key for field `io_a2p`.
constexpr char IO_A2S[] = "io_a2s"; // IO module persisted key for field `io_a2s`.
constexpr char IO_A2X[] = "io_a2x"; // IO module persisted key for field `io_a2x`.
constexpr char IO_A30[] = "io_a30"; // IO module persisted key for field `io_a30`.
constexpr char IO_A31[] = "io_a31"; // IO module persisted key for field `io_a31`.
constexpr char IO_A3C[] = "io_a3c"; // IO module persisted key for field `io_a3c`.
constexpr char IO_A3N[] = "io_a3n"; // IO module persisted key for field `io_a3n`.
constexpr char IO_A3NM[] = "io_a3nm"; // IO module persisted key for field `io_a3nm`.
constexpr char IO_A3P[] = "io_a3p"; // IO module persisted key for field `io_a3p`.
constexpr char IO_A3S[] = "io_a3s"; // IO module persisted key for field `io_a3s`.
constexpr char IO_A3X[] = "io_a3x"; // IO module persisted key for field `io_a3x`.
constexpr char IO_A40[] = "io_a40"; // IO module persisted key for field `io_a40`.
constexpr char IO_A41[] = "io_a41"; // IO module persisted key for field `io_a41`.
constexpr char IO_A4C[] = "io_a4c"; // IO module persisted key for field `io_a4c`.
constexpr char IO_A4N[] = "io_a4n"; // IO module persisted key for field `io_a4n`.
constexpr char IO_A4NM[] = "io_a4nm"; // IO module persisted key for field `io_a4nm`.
constexpr char IO_A4P[] = "io_a4p"; // IO module persisted key for field `io_a4p`.
constexpr char IO_A4S[] = "io_a4s"; // IO module persisted key for field `io_a4s`.
constexpr char IO_A4X[] = "io_a4x"; // IO module persisted key for field `io_a4x`.
constexpr char IO_A50[] = "io_a50"; // IO module persisted key for field `io_a50`.
constexpr char IO_A51[] = "io_a51"; // IO module persisted key for field `io_a51`.
constexpr char IO_A5C[] = "io_a5c"; // IO module persisted key for field `io_a5c`.
constexpr char IO_A5N[] = "io_a5n"; // IO module persisted key for field `io_a5n`.
constexpr char IO_A5NM[] = "io_a5nm"; // IO module persisted key for field `io_a5nm`.
constexpr char IO_A5P[] = "io_a5p"; // IO module persisted key for field `io_a5p`.
constexpr char IO_A5S[] = "io_a5s"; // IO module persisted key for field `io_a5s`.
constexpr char IO_A5X[] = "io_a5x"; // IO module persisted key for field `io_a5x`.
constexpr char IO_ADS[] = "io_ads"; // IO module persisted key for field `io_ads`.
constexpr char IO_AEAD[] = "io_aead"; // IO module persisted key for field `io_aead`.
constexpr char IO_AGAI[] = "io_agai"; // IO module persisted key for field `io_agai`.
constexpr char IO_AIAD[] = "io_aiad"; // IO module persisted key for field `io_aiad`.
constexpr char IO_ARAT[] = "io_arat"; // IO module persisted key for field `io_arat`.
constexpr char IO_D0AH[] = "io_d0ah"; // IO module persisted key for field `io_d0ah`.
constexpr char IO_D0IN[] = "io_d0in"; // IO module persisted key for field `io_d0in`.
constexpr char IO_D0MO[] = "io_d0mo"; // IO module persisted key for field `io_d0mo`.
constexpr char IO_D0NM[] = "io_d0nm"; // IO module persisted key for field `io_d0nm`.
constexpr char IO_D0PM[] = "io_d0pm"; // IO module persisted key for field `io_d0pm`.
constexpr char IO_D0PN[] = "io_d0pn"; // IO module persisted key for field `io_d0pn`.
constexpr char IO_D1AH[] = "io_d1ah"; // IO module persisted key for field `io_d1ah`.
constexpr char IO_D1IN[] = "io_d1in"; // IO module persisted key for field `io_d1in`.
constexpr char IO_D1MO[] = "io_d1mo"; // IO module persisted key for field `io_d1mo`.
constexpr char IO_D1NM[] = "io_d1nm"; // IO module persisted key for field `io_d1nm`.
constexpr char IO_D1PM[] = "io_d1pm"; // IO module persisted key for field `io_d1pm`.
constexpr char IO_D1PN[] = "io_d1pn"; // IO module persisted key for field `io_d1pn`.
constexpr char IO_D2AH[] = "io_d2ah"; // IO module persisted key for field `io_d2ah`.
constexpr char IO_D2IN[] = "io_d2in"; // IO module persisted key for field `io_d2in`.
constexpr char IO_D2MO[] = "io_d2mo"; // IO module persisted key for field `io_d2mo`.
constexpr char IO_D2NM[] = "io_d2nm"; // IO module persisted key for field `io_d2nm`.
constexpr char IO_D2PM[] = "io_d2pm"; // IO module persisted key for field `io_d2pm`.
constexpr char IO_D2PN[] = "io_d2pn"; // IO module persisted key for field `io_d2pn`.
constexpr char IO_D3AH[] = "io_d3ah"; // IO module persisted key for field `io_d3ah`.
constexpr char IO_D3IN[] = "io_d3in"; // IO module persisted key for field `io_d3in`.
constexpr char IO_D3MO[] = "io_d3mo"; // IO module persisted key for field `io_d3mo`.
constexpr char IO_D3NM[] = "io_d3nm"; // IO module persisted key for field `io_d3nm`.
constexpr char IO_D3PM[] = "io_d3pm"; // IO module persisted key for field `io_d3pm`.
constexpr char IO_D3PN[] = "io_d3pn"; // IO module persisted key for field `io_d3pn`.
constexpr char IO_D4AH[] = "io_d4ah"; // IO module persisted key for field `io_d4ah`.
constexpr char IO_D4IN[] = "io_d4in"; // IO module persisted key for field `io_d4in`.
constexpr char IO_D4MO[] = "io_d4mo"; // IO module persisted key for field `io_d4mo`.
constexpr char IO_D4NM[] = "io_d4nm"; // IO module persisted key for field `io_d4nm`.
constexpr char IO_D4PM[] = "io_d4pm"; // IO module persisted key for field `io_d4pm`.
constexpr char IO_D4PN[] = "io_d4pn"; // IO module persisted key for field `io_d4pn`.
constexpr char IO_D5AH[] = "io_d5ah"; // IO module persisted key for field `io_d5ah`.
constexpr char IO_D5IN[] = "io_d5in"; // IO module persisted key for field `io_d5in`.
constexpr char IO_D5MO[] = "io_d5mo"; // IO module persisted key for field `io_d5mo`.
constexpr char IO_D5NM[] = "io_d5nm"; // IO module persisted key for field `io_d5nm`.
constexpr char IO_D5PM[] = "io_d5pm"; // IO module persisted key for field `io_d5pm`.
constexpr char IO_D5PN[] = "io_d5pn"; // IO module persisted key for field `io_d5pn`.
constexpr char IO_D6AH[] = "io_d6ah"; // IO module persisted key for field `io_d6ah`.
constexpr char IO_D6IN[] = "io_d6in"; // IO module persisted key for field `io_d6in`.
constexpr char IO_D6MO[] = "io_d6mo"; // IO module persisted key for field `io_d6mo`.
constexpr char IO_D6NM[] = "io_d6nm"; // IO module persisted key for field `io_d6nm`.
constexpr char IO_D6PM[] = "io_d6pm"; // IO module persisted key for field `io_d6pm`.
constexpr char IO_D6PN[] = "io_d6pn"; // IO module persisted key for field `io_d6pn`.
constexpr char IO_D7AH[] = "io_d7ah"; // IO module persisted key for field `io_d7ah`.
constexpr char IO_D7IN[] = "io_d7in"; // IO module persisted key for field `io_d7in`.
constexpr char IO_D7MO[] = "io_d7mo"; // IO module persisted key for field `io_d7mo`.
constexpr char IO_D7NM[] = "io_d7nm"; // IO module persisted key for field `io_d7nm`.
constexpr char IO_D7PM[] = "io_d7pm"; // IO module persisted key for field `io_d7pm`.
constexpr char IO_D7PN[] = "io_d7pn"; // IO module persisted key for field `io_d7pn`.
constexpr char IO_DIN[] = "io_din"; // IO module persisted key for field `io_din`.
constexpr char IO_DS[] = "io_ds"; // IO module persisted key for field `io_ds`.
constexpr char IO_EN[] = "io_en"; // IO module persisted key for field `io_en`.
constexpr char IO_PCFAD[] = "io_pcfad"; // IO module persisted key for field `io_pcfad`.
constexpr char IO_PCFAL[] = "io_pcfal"; // IO module persisted key for field `io_pcfal`.
constexpr char IO_PCFEN[] = "io_pcfen"; // IO module persisted key for field `io_pcfen`.
constexpr char IO_PCFMK[] = "io_pcfmk"; // IO module persisted key for field `io_pcfmk`.
constexpr char IO_SCL[] = "io_scl"; // IO module persisted key for field `io_scl`.
constexpr char IO_SDA[] = "io_sda"; // IO module persisted key for field `io_sda`.
constexpr char IO_TREN[] = "io_tren"; // IO module persisted key for field `io_tren`.
constexpr char IO_TRMS[] = "io_trms"; // IO module persisted key for field `io_trms`.
}  // namespace Io

namespace I2cCfg {
constexpr char ClientEnabled[] = "ic_cli_en"; // I2C cfg client enabled.
constexpr char ClientUseIoBus[] = "ic_cli_iob"; // I2C cfg client uses IO I2C pin mapping.
constexpr char ClientBus[] = "ic_cli_bus"; // I2C cfg client bus index (0/1).
constexpr char ClientSda[] = "ic_cli_sda"; // I2C cfg client SDA pin.
constexpr char ClientScl[] = "ic_cli_scl"; // I2C cfg client SCL pin.
constexpr char ClientFreq[] = "ic_cli_frq"; // I2C cfg client bus frequency.
constexpr char ClientAddr[] = "ic_cli_adr"; // I2C cfg client target slave address.

constexpr char ServerEnabled[] = "ic_srv_en"; // I2C cfg server enabled.
constexpr char ServerUseIoBus[] = "ic_srv_iob"; // I2C cfg server uses IO I2C pin mapping.
constexpr char ServerBus[] = "ic_srv_bus"; // I2C cfg server bus index (0/1).
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
constexpr char FiltrationCalcStart[] = "pl_fcst"; // Pool logic runtime key for calculated filtration start hour.
constexpr char FiltrationCalcStop[] = "pl_fcen"; // Pool logic runtime key for calculated filtration stop hour.
}  // namespace PoolLogic

namespace PoolDevice {
/** @brief printf format for per-slot `enabled` key (example `pd0en`). */
constexpr char EnabledFmt[] = "pd%uen"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
/** @brief printf format for per-slot `type` key (example `pd0ty`). */
constexpr char TypeFmt[] = "pd%uty"; // Pool device module key template; `%u` is replaced by slot index before NVS access.
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

}  // namespace NvsKeys
