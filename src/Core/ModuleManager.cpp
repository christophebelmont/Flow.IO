/**
 * @file ModuleManager.cpp
 * @brief Implementation file.
 */
#include "ModuleManager.h"
#include "Core/Log.h"
#include <cstring>

#define LOG_TAG_CORE "ModManag"

static void logRegisteredModules(Module* modules[], uint8_t count) {
    ///Logger::log(LogLevel::Info, "MOD", "Registered modules (%u):", count);
    for (uint8_t i = 0; i < count; ++i) {
        ///Logger::log(LogLevel::Info, "MOD", " - %s", modules[i]->moduleId());
    }
}

static void dbgDumpModules(Module* modules[], uint8_t count) {
    Serial.printf("[MOD] Registered modules (%u):\n", count);
    for (uint8_t i = 0; i < count; ++i) {
        if (!modules[i]) continue;
        Serial.printf("  - %s deps=%u\n", modules[i]->moduleId(), modules[i]->dependencyCount());
        for (uint8_t d = 0; d < modules[i]->dependencyCount(); ++d) {
            const char* dep = modules[i]->dependency(d);
            Serial.printf("      -> %s\n", dep ? dep : "(null)");
        }
    }
    Serial.flush();
    delay(50);
}

void ModuleManager::add(Module* m) { modules[count++] = m; }

Module* ModuleManager::findById(const char* id) {
    for (uint8_t i = 0; i < count; ++i)
        if (strcmp(modules[i]->moduleId(), id) == 0) return modules[i];
    return nullptr;
}

bool ModuleManager::buildInitOrder() {
    Log::debug(LOG_TAG_CORE, "buildInitOrder: count=%u", (unsigned)count);
    /// Kahn topo-sort
    bool placed[MAX_MODULES] = {0};
    orderedCount = 0;

    for (uint8_t pass = 0; pass < count; ++pass) {
        bool progress = false;

        for (uint8_t i = 0; i < count; ++i) {
            Module* m = modules[i];
            if (!m || placed[i]) continue;

            /// Check if all dependencies are already placed
            bool depsOk = true;
            const uint8_t depCount = m->dependencyCount();

            for (uint8_t d = 0; d < depCount; ++d) {
                const char* depId = m->dependency(d);
                if (!depId) continue;

                Module* dep = findById(depId);
                if (!dep) {
                    Serial.printf("[MOD][ERR] Missing dependency: module='%s' requires='%s'\n",
                                  m->moduleId(), depId);
                    Serial.flush();
                    delay(20);
            Log::error(LOG_TAG_CORE, "missing dependency: module=%s requires=%s",
                               m->moduleId(), depId);
                    return false;
                }

                /// Is dep already placed in ordered[] ?
                bool depPlaced = false;
                for (uint8_t j = 0; j < count; ++j) {
                    if (modules[j] == dep) {
                        depPlaced = placed[j];
                        break;
                    }
                }

                if (!depPlaced) {
                    depsOk = false;
                    break;
                }
            }

            if (depsOk) {
                ordered[orderedCount++] = m;
                placed[i] = true;
                progress = true;
            }
        }
        
        if (orderedCount == count) {
            return true;
        }

        if (!progress) {
            Serial.println("[MOD][ERR] Cyclic deps detected (or unresolved deps)");
            Serial.println("[MOD] Remaining not placed:");
            for (uint8_t i = 0; i < count; ++i) {
                if (modules[i] && !placed[i]) {
                    Serial.printf("   * %s\n", modules[i]->moduleId());
                }
            }
            Serial.flush();
            delay(50);
            Log::error(LOG_TAG_CORE, "cyclic or unresolved deps detected");
            return false;
        }
    }

    Log::debug(LOG_TAG_CORE, "buildInitOrder: success (ordered=%u)", (unsigned)orderedCount);
    return true;
}


bool ModuleManager::initAll(ConfigStore& cfg, ServiceRegistry& services) {
    Log::debug(LOG_TAG_CORE, "initAll: moduleCount=%u", (unsigned)count);
    /*Serial.printf("[MOD] moduleCount=%d\n", count);
    for (int i=0;i<count;i++){
        Serial.printf("[MOD] module[%d]=%s\n", i, modules[i]->moduleId());
    }
    Serial.flush();
    delay(20);*/

    // dbgDumpModules(modules, count); // debug dump

    if (!buildInitOrder()) return false;

    for (uint8_t i = 0; i < orderedCount; ++i) {
        ///Logger::log(LogLevel::Info, "MOD", "Init %s", ordered[i]->moduleId());
        Log::debug(LOG_TAG_CORE, "init: %s", ordered[i]->moduleId());
        ordered[i]->init(cfg, services);
    }

    /// Load persistent config after all modules registered their variables.
    cfg.loadPersistent();

    for (uint8_t i = 0; i < orderedCount; ++i) {
        ordered[i]->onConfigLoaded(cfg, services);
    }

    for (uint8_t i = 0; i < orderedCount; ++i) {
        ///Logger::log(LogLevel::Info, "MOD", "Start %s", ordered[i]->moduleId());
        if (!ordered[i]->hasTask()) {
        continue;
    }
        Log::debug(LOG_TAG_CORE, "startTask: %s", ordered[i]->moduleId());
        ordered[i]->startTask();
    }

    wireCoreServices(services, cfg);
    Log::debug(LOG_TAG_CORE, "initAll: done");
    
    return true;
}

void ModuleManager::wireCoreServices(ServiceRegistry& services, ConfigStore& config) {

  auto* ebService = services.get<EventBusService>("eventbus");
  if (ebService && ebService->bus) {
    config.setEventBus(ebService->bus);
    Log::debug(LOG_TAG_CORE, "wireCoreServices: eventbus wired");
  }
}
