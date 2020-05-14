#include <afina/coroutine/Engine.h>

#include <cassert>
#include <csetjmp>
#include <cstring>

namespace Afina {
namespace Coroutine {

Engine::~Engine() {
    for (auto coro = alive; coro != nullptr;) {
        auto tmp = coro;
        coro = coro->next;
        delete tmp;
    }

    for (auto coro = blocked; coro != nullptr;) {
        auto tmp = coro;
        coro = coro->next;
        delete tmp;
    }
}

void Engine::Store(context &ctx) {
    char storeBeginAddress;
    assert(ctx.Hight != nullptr && ctx.Low != nullptr);
    // this condition used for different architectures,
    // where stack low addresses can be greater than high addresses
    if (&storeBeginAddress > ctx.Low) {
        ctx.Hight = &storeBeginAddress;
    } else {
        ctx.Low = &storeBeginAddress;
    }
    auto stackSize = ctx.Hight - ctx.Low;
    // we should allocate memory for new stack copy if it was't allocated yet
    // or current stack size isn't big enough or current stack size is too big
    if (stackSize > std::get<1>(ctx.Stack) || stackSize * 2 < std::get<1>(ctx.Stack)) {
        delete[] std::get<0>(ctx.Stack);
        std::get<0>(ctx.Stack) = new char[stackSize];
        std::get<1>(ctx.Stack) = stackSize;
    }
    memcpy(std::get<0>(ctx.Stack), ctx.Low, stackSize);
}

void Engine::Restore(context &ctx) {
    char restoreBeginAddress;
    // used for saving stack of restored coroutine
    while (&restoreBeginAddress <= ctx.Hight && &restoreBeginAddress >= ctx.Low) {
        Restore(ctx);
    }
    // now we can restore coroutine's stack without changing our stack
    std::memcpy(ctx.Low, std::get<0>(ctx.Stack), ctx.Hight - ctx.Low);
    cur_routine = &ctx;
    // run coroutine from the point where it was stopped
    longjmp(ctx.Environment, 1);
}

void Engine::Enter(Engine::context *ctx) {
    assert(cur_routine != nullptr);
    if (cur_routine != idle_ctx) {
        if (setjmp(cur_routine->Environment) > 0) {
            return;
        }
        Store(*cur_routine);
    }
    Restore(*ctx);
}

void Engine::yield() {
    // we have no alive coroutines or we have only one alive coroutine, that is current
    if (!alive || (cur_routine == alive && !alive->next)) return;
    // choose the next coroutine
    context *nextCoro;
    if (cur_routine == alive) {
        nextCoro = alive->next;
    } else {
        nextCoro = alive;
    }
    // run the next alive coroutine
    Enter(nextCoro);
}

void Engine::sched(void *coro) {
    auto nextCoro = static_cast<context *>(coro);
    if (nextCoro == nullptr) {
        yield();
    }
    // we will do nothing if the next coroutine is blocked
    if (nextCoro == cur_routine || nextCoro->isBlocked) {
        return;
    }
    // run the next coroutine
    Enter(nextCoro);
}

void Engine::block(void *coro) {
    context *blockedCoro;
    // if argument coro == nullptr, then we should block current coroutine
    if (!coro) {
        blockedCoro = cur_routine;
    } else {
        blockedCoro = static_cast<context *>(coro);
    }
    // we shouldn't block coroutine if it's already blocked
    if (!blockedCoro || blockedCoro->isBlocked) {
        return;
    }
    blockedCoro->isBlocked = true;
    // delete coroutine from the list of alive coroutines
    if (alive == blockedCoro) {
        alive = alive->next;
    }
    if (blockedCoro->prev) {
        blockedCoro->prev->next = blockedCoro->next;
    }
    if (blockedCoro->next) {
        blockedCoro->next->prev = blockedCoro->prev;
    }
    // add coroutine to the list of blocked coroutines
    blockedCoro->prev = nullptr;
    blockedCoro->next = blocked;
    blocked = blockedCoro;
    if (blocked->next) {
        blocked->next->prev = blockedCoro;
    }
    if (blockedCoro == cur_routine) {
        Enter(idle_ctx);
    }
}

void Engine::unblock(void *coro) {
    auto unblockedCoro = static_cast<context *>(coro);
    // we shouldn't unblock coroutine if it's already unblocked
    if (!unblockedCoro || !unblockedCoro->isBlocked) {
        return;
    }
    unblockedCoro->isBlocked = false;
    // delete coroutine from the list of blocked coroutines
    if (blocked == unblockedCoro) {
        blocked = blocked->next;
    }
    if (unblockedCoro->prev) {
        unblockedCoro->prev->next = unblockedCoro->next;
    }
    if (unblockedCoro->next) {
        unblockedCoro->next->prev = unblockedCoro->prev;
    }
    // add coroutine to the list of alive coroutines
    unblockedCoro->prev = nullptr;
    unblockedCoro->next = alive;
    alive = unblockedCoro;
    if (alive->next) {
        alive->next->prev = unblockedCoro;
    }
}

} // namespace Coroutine
} // namespace Afina
