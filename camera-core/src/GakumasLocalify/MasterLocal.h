#ifndef GAKUMAS_LOCALIFY_MASTERLOCAL_H
#define GAKUMAS_LOCALIFY_MASTERLOCAL_H

#include <string>
#include <memory>

namespace GakumasLocal::MasterLocal {
    // Opaque published table set. Parsing builds a fresh snapshot off thread;
    // initial hosted publication precedes every detour, and later publications
    // run on the Unity main thread together with the local translation maps.
    struct Snapshot;

    std::shared_ptr<const Snapshot> ParseSnapshot();
    void PublishSnapshot(std::shared_ptr<const Snapshot> snapshot);
    void LoadData();

    void LocalizeMasterItem(void* item, const std::string& tableName);
}

#endif //GAKUMAS_LOCALIFY_MASTERLOCAL_H
