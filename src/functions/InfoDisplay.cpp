#include "InfoDisplay.h"

#include <MC/Actor.hpp>
#include <MC/BaseCircuitComponent.hpp>
#include <MC/Biome.hpp>
#include <MC/Block.hpp>
#include <MC/BlockActor.hpp>
#include <MC/BlockSource.hpp>
#include <MC/Brightness.hpp>
#include <MC/CircuitSceneGraph.hpp>
#include <MC/CircuitSystem.hpp>
#include <MC/Dimension.hpp>
#include <MC/Material.hpp>
#include <MC/NavigationComponent.hpp>
#include <MC/Player.hpp>
#include <MC/RedstoneTorchCapacitor.hpp>
#include <unordered_map>
#include <vector>

#include "CommandHelper.h"
#include "DataConverter.h"
#include "Global.h"
#include "MC/PrettySnbtFormat.hpp"
#include "Msg.h"
#include "Particle.h"
#include "TBlockPos.h"
#include "TrAPI.h"
#include "TrapdoorMod.h"
#include "Utils.h"
namespace trapdoor {
    namespace {

        struct ComponentItem {
            BaseCircuitComponent *mComponent = nullptr;  // 0 * 4 - 1 * 4
            int mDampening = 0;                          // 2 * 4
            BlockPos mPos;                               // 3 * 4 - 5 * 4
            unsigned char facing{};                      // 6 * 4
            bool mDirectlyPowered = false;               // 6* 4
            int mData = 0;                               // 7*4
        };

        struct PendingEntry {
            std::unique_ptr<BaseCircuitComponent> mComponent;
            BlockPos mPos;
            BaseCircuitComponent *mRawComponentPtr;
        };

        /*
         *Core data structure
         */
        struct TCircuitSceneGraph {
            std::unordered_map<BlockPos, std::unique_ptr<BaseCircuitComponent>> mAllComponents;
            std::vector<ComponentItem> mActiveComponents;
            std::unordered_map<BlockPos, std::vector<ComponentItem>> mActiveComponentsPerChunk;
            std::unordered_map<BlockPos, std::vector<ComponentItem>> mPowerAssociationMap;
        };

        static_assert(sizeof(ComponentItem) == 32);

        std::string printableNBT(const std::unique_ptr<CompoundTag> &nbt) {
            return nbt->toPrettySNBT(true);
        }
        ActionResult getNBTString(const std::unique_ptr<CompoundTag> &nbt,
                                  const std::string &path) {
            if (path.empty()) {
                return {printableNBT(nbt), true};
            } else {
                bool success = false;
                auto str = getNBTInfoFromPath(nbt, path, success);
                return {str, success};
            }
        }
    }  // namespace
    ActionResult displayEntityInfo(Player *player, Actor *a, bool nbt, const std::string &path) {
        if (!player) return ErrorPlayerNeed();
        if (!a) {
            return {"No actor", false};
        }
        TextBuilder builder;
        if (nbt) {
            return getNBTString(a->getNbt(), path);
        }

        builder.sText(TextBuilder::AQUA, "Base: \n")
            .text(" - type / UID: ")
            .sTextF(TextBuilder::GREEN, "%s    %llx\n", a->getTypeName().c_str(),
                    a->getUniqueID().get())
            .text(" - Pos/DeltaPos: ")
            .sTextF(TextBuilder::GREEN, "%s / %s|%s", fromVec3(a->getPos()).toString().c_str(),
                    fromVec3(a->getPosDelta()).toString().c_str())
            .text(" - AABB: ")
            .sTextF(TextBuilder::GREEN, "%s", fromAABB(a->getAABB()).ToString().c_str())
            .text("\n")
            .text(" - Surface: ")
            .sTextF(TextBuilder::GREEN, "%d\n", a->isSurfaceMob());
        return {builder.get(), true};
    }

    ActionResult displayBlockInfo(Player *p, const BlockPos &position, bool nbt,
                                  const std::string &path) {
        if (!p) return ErrorPlayerNeed();
        auto pos = position;
        if (pos == BlockPos::MAX) {
            pos = trapdoor::getLookAtPos(p);
        }

        if (pos == BlockPos::MAX) {
            return {"Get blockName failure", false};
        }
        auto &b = p->getRegion().getBlock(pos);
        trapdoor::TextBuilder builder;
        if (nbt) {
            if (b.hasBlockEntity()) {
                auto be = p->getRegion().getBlockEntity(pos);
                if (be) {
                    return getNBTString(be->getNbt(), path);
                }
            } else {
                return {"No NBT data", false};
            }
        }

        builder.sText(trapdoor::TextBuilder::AQUA, "Base:\n")
            .text(" - Name / Type: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%s / %s\n", b.getName().c_str(),
                    b.getTypeName().c_str())
            .text(" - ID / RTID: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%d / %d\n", b.getId(), b.getRuntimeId())
            .text(" - Variant: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%d\n", b.getVariant())
            .text(" - CanInstanceTick: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%d\n", b.canInstatick())
            .text(" - BlockEntity: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%d\n", b.hasBlockEntity())
            .text(" - IsSolid: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%d\n", b.isSolid());
        auto &m = b.getMaterial();
        builder.sText(trapdoor::TextBuilder::AQUA, "Material:\n")
            .text(" - Motion: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%d\n", m.getBlocksMotion())
            .text(" - TopSolid: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%d\n", m.isTopSolid(false, false))
            .text(" - IsSolid: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%d\n", m.isSolid())
            .text(" - IsSolidBlocking: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%d\n", m.isSolidBlocking())
            .text(" - Translucency: ")
            .sTextF(trapdoor::TextBuilder::GREEN, "%.3f\n", m.getTranslucency());
        return {builder.get(), true};
    }

    bool displayEnvInfo() { return true; }

    ActionResult displayRedstoneCompInfo(Dimension *d, const BlockPos &pos,
                                         const std::string &type) {
        if (!d) return ErrorDimension();
        if (pos == BlockPos::MAX) return ErrorPosition();

        auto &cs = d->getCircuitSystem();
        auto &graph = getCircuitSceneGraph(&cs);
        auto *g = reinterpret_cast<TCircuitSceneGraph *>(&graph);
        // 获取红石组件
        if (type == "chunk") {
            auto chunkPos = fromBlockPos(pos).toChunkPos();
            BlockPos cp{chunkPos.x * 16, 0, chunkPos.z * 16};
            auto iter = g->mActiveComponentsPerChunk.find(cp);
            if (iter != g->mActiveComponentsPerChunk.end()) {
                for (auto &l : iter->second) {
                    auto lPos = l.mPos;
                    trapdoor::shortHighlightBlock({lPos.x, lPos.y, lPos.z}, PCOLOR::BLUE,
                                                  d->getDimensionId());
                }
            }

            return {"", true};
        }

        auto comp = graph.getBaseComponent(pos);
        if (!comp) {
            return {"Not an redstone component", false};
        }

        if (type == "signal") {
            TextBuilder builder;
            builder.text("Signal: ").num(comp->getStrength()).text("\n");
            auto &list = dAccess<std::vector<ComponentItem>, 8>(comp);
            for (auto &source : list) {
                auto p = source.mPos;
                builder.textF("Pos: [%s] Damp: %d  Dp: %d   Signal: %d\n", p.toString().c_str(),
                              source.mDampening, source.mDirectlyPowered,
                              source.mComponent->getStrength());
            }
            return {builder.get(), true};
        }

        if (type == "torch") {
            return {"", true};
        }

        if (type != "conn") return {"", true};
        // link
        // 高亮自身
        trapdoor::shortHighlightBlock({pos.x, pos.y, pos.z}, PCOLOR::GREEN, d->getDimensionId());
        // 高亮被自身激活的原件
        auto it = g->mPowerAssociationMap.find(pos);
        if (it != g->mPowerAssociationMap.end()) {
            for (auto &c : it->second) {
                auto p = c.mPos;
                trapdoor::shortHighlightBlock({p.x, p.y, p.z}, PCOLOR::YELLOW, d->getDimensionId());
            }
        }

        // 高亮可以激活自身的原件
        auto &list = dAccess<std::vector<ComponentItem>, 8>(comp);
        for (auto &source : list) {
            auto p = source.mPos;
            trapdoor::shortHighlightBlock({p.x, p.y, p.z}, PCOLOR::RED, d->getDimensionId());
        }

        return {"", true};
    }
}  // namespace trapdoor

THook(void,
      "?findRelationships@CircuitSceneGraph@@AEAAXAEBVBlockPos@@PEAVBaseCircuitComponent@@"
      "PEAVBlockSource@@@Z",
      void *graph, BlockPos const &pos, BaseCircuitComponent *comp, class BlockSource *bs) {
    trapdoor::TextBuilder builder;
    builder.sTextF(trapdoor::TB::BOLD | trapdoor::TB::GREEN, "[%s] ", pos.toString().c_str())
        .text("Start a connection build\n");
    trapdoor::mod().getEventTriggerMgr().broadcastMessage(trapdoor::BuildConnection, builder.get());
    original(graph, pos, comp, bs);
}

// 红石线

std::string compTypeToStr(uint64_t type) {
    auto color = trapdoor::TextBuilder::WHITE;
    std::string str = "unk";
    if (type == 0x100000) {
        color = trapdoor::TB::RED;
        str = "Wine";
    } else if (type == 0x80000) {
        color = trapdoor::TB::DARK_RED;
        str = "PowB";
    } else if (type == 0x200000) {
        color = trapdoor::TB::AQUA;
        str = "Capa";
    } else if (type == 0x20000) {
        color = trapdoor::TB::GRAY;
        str = "Cons";
    }
    trapdoor::TextBuilder builder;
    builder.sTextF(color, "%s", str.c_str());
    return builder.get();
}

class TCircuitTrackingInfo {
   public:
    struct TEntry {
        BaseCircuitComponent *mComponent = nullptr;  // 0 - 8
        BlockPos mPos;                               // 9 ~20
        trapdoor::TFACING mDirection;                // 21 ~ ?
        uint64_t type{};
    };

    static_assert(sizeof(TEntry) == 0x20);
    TEntry mCurrent{};
    TEntry mPower{};
    TEntry mNearest{};
    TEntry m2ndNearest{};
    bool mDirectlyPowered = true;
    int mData = 0;
    int mDampening;
};

std::string buildMsg(TCircuitTrackingInfo *info, int *damping, bool *dirPow) {
    auto p = info->mCurrent.mPos;
    auto ne = info->mNearest;
    auto ned = trapdoor::facingToString(ne.mDirection);
    auto neTypeStr = compTypeToStr(ne.type);
    if (neTypeStr.find("unk") != std::string::npos) {
        printf("Unknown type: 0x%llx\n", ne.type);
    }

    return fmt::format(" [Add S] [{}]--{}-->{} ({},{},{}) idp:{} / dp: {} / dp: {}", neTypeStr, ned,
                       compTypeToStr(info->mCurrent.type), p.x, p.y, p.z, info->mDampening,
                       *damping, *dirPow);
}

THook(bool,
      "?addSource@TransporterComponent@@UEAA_NAEAVCircuitSceneGraph@@AEBVCircuitTrackingInfo@@"
      "AEAHAEA_N@Z",
      BaseCircuitComponent *self, CircuitSceneGraph *graph, TCircuitTrackingInfo *info,
      int *damping, bool *directPowered) {
    auto res = original(self, graph, info, damping, directPowered);
    if (res) {
        trapdoor::mod().getEventTriggerMgr().broadcastMessage(
            trapdoor::BuildConnection, buildMsg(info, damping, directPowered));
    }
    return res;
}

// 比较器
THook(bool,
      "?addSource@ComparatorCapacitor@@UEAA_NAEAVCircuitSceneGraph@@AEBVCircuitTrackingInfo@@"
      "AEAHAEA_N@Z",
      void *self, CircuitSceneGraph *graph, TCircuitTrackingInfo *info, int *damping,
      bool *directPowered) {
    auto res = original(self, graph, info, damping, directPowered);
    if (res) {
        trapdoor::mod().getEventTriggerMgr().broadcastMessage(
            trapdoor::BuildConnection, buildMsg(info, damping, directPowered));
    }
    return res;
}

// 消费者

THook(bool,
      "?addSource@ConsumerComponent@@UEAA_NAEAVCircuitSceneGraph@@AEBVCircuitTrackingInfo@@AEAHAEA_"
      "N@Z",
      void *self, CircuitSceneGraph *graph, TCircuitTrackingInfo *info, int *damping,
      bool *directPowered) {
    auto res = original(self, graph, info, damping, directPowered);
    if (res) {
        trapdoor::mod().getEventTriggerMgr().broadcastMessage(
            trapdoor::BuildConnection, buildMsg(info, damping, directPowered));
    }
    return res;
}

// THook(bool, "?trackPowerSource@BaseCircuitComponent@@IEAA_NAEBVCircuitTrackingInfo@@H_NH@Z",
//       void *self, void *info, int damp, bool dp, int data) {
//     auto res = original(self, info, damp, dp, data);
//     if (res) {
//         auto msg = fmt::format(" [tra PS] damp: {} dp {} data {}", damp, dp, data);
//         trapdoor::mod().getEventTriggerMgr().broadcastMessage(trapdoor::BuildConnection, msg);
//
//     }
//     return res;
// }