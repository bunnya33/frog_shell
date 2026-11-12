#pragma once
#include <QtCore>
#include <atomic>
namespace rs {
// One process-wide operation epoch. Background work already accepted remains alive.
class OperationGate {
  public:
    static bool locked() { return state.load(std::memory_order_acquire)&1; }
    static quint64 token() { return state.load(std::memory_order_acquire); }
    static bool accepts(quint64 token) { return !(token&1) && token==state.load(std::memory_order_acquire); }
    static void setLocked(bool locked) {
        auto old=state.load();
        while(bool(old&1)!=locked && !state.compare_exchange_weak(old,((old>>1)+1)*2+(locked?1:0))) {}
    }
    static void lockAgain() { auto old=state.load();while(!state.compare_exchange_weak(old,((old>>1)+1)*2+1)){} }
  private:
    inline static std::atomic<quint64> state{0};
};
}
