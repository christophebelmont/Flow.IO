/**
 * @file DataStore.cpp
 * @brief Implementation file.
 */
#include "Core/DataStore/DataStore.h"
#include "Core/ModuleId.h"

void DataStore::publishChanged(DataKey key)
{
    if (!_bus) return;
    DataChangedPayload p{ key };
    _bus->post(EventId::DataChanged, &p, sizeof(p), ModuleId::DataStore);
}

void DataStore::notifyChanged(DataKey key)
{
    publishChanged(key);
}
