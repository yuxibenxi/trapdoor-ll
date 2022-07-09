#ifndef TRAPDOOR_EVENT_H
#define TRAPDOOR_EVENT_H

namespace tr {
    void subscribeItemUseOnEvent();
    void subscribeItemUseEvent();
    void subscribePlayerDieEvent();
    void subscribePlayerStartDestroyBlockEvent();
    void subscribePlayerPlaceBlockEvent();
    void subscribePlayerInventoryChangeEvent();

    inline void SubscribeEvents() {
        subscribeItemUseOnEvent();
        subscribeItemUseEvent();
        subscribePlayerDieEvent();
        subscribePlayerStartDestroyBlockEvent();
        subscribePlayerPlaceBlockEvent();
        subscribePlayerInventoryChangeEvent();
    }

}  // namespace tr

#endif
