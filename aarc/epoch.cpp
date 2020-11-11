// epoch-based garbage collection
//
// in imitation of Crossbeam https://github.com/crossbeam-rs/crossbeam in an
// attempt to deeply understand it


#include <map>
#include <optional>
#include <thread>
#include <vector>

#include "atomic.hpp"
#include "corrode.hpp"
#include "epoch.hpp"
#include "tagged.hpp"

#include <catch2/catch.hpp>

using namespace aarc;

struct Local;

struct Global {
    
    mutable u64 epoch;
    Atomic<TaggedPtr<Local>> head;
    
    static Global const& get() {
        // global is deliberately leaked at shutdown
        static Global* global = new Global {
            0,
            TaggedPtr<Local> { nullptr }
        };
        return *global;
    }
    
};

struct Local {
    
private:
    
    static Local* make() {
        // insert node at head of list
        Atomic<TaggedPtr<Local>> const& head = Global::get().head;
        Local* desired = new Local { 0, head.load(std::memory_order_relaxed) };
        while (!head.compare_exchange_weak(desired->next,
                                           TaggedPtr<Local> { desired },
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {}
        return desired;
    }
    
    void mark() const {
        // mark node for deletion
        this->next.fetch_or(1, std::memory_order_release);
    }
    
public:
    
    Atomic<u64> epoch;
    Atomic<TaggedPtr<Local>> next;
    
    static Local const& get() {
        thread_local struct Guard {
            Local const* local;
            Guard() : local(make()) {}
            ~Guard() { local->mark(); }
        } guard;
        return *guard.local;
    }
    
    template<typename F>
    void defer(F&&) const {}
    
    // be careful of coroutine heap allocation overhead; but this should be
    // infrequently called
    Generator<u64> epochs() const {
        Atomic<TaggedPtr<Local>> const* pred = &Global::get().head;
        TaggedPtr<Local> curr = pred->load(std::memory_order_acquire);
        while (curr.ptr) {
            TaggedPtr<Local> next = curr->next.load(std::memory_order_relaxed);
            if (!next.tag) {
                // read the epoch
                co_yield curr->epoch.load(std::memory_order_relaxed);
            } else if (!curr.tag) {
                // node is marked for deletion and predecessor is not, so we can
                // attempt to delete it
                if (pred->compare_exchange_strong(curr,
                                                  next & 0,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed)) {
                    // we won the race to delete the node, now we defer its actual
                    // reclamation
                    this->defer([ptr = curr.ptr] {
                        delete ptr;
                    });
                }
                continue;
            }
            // advance
            pred = &curr->next;
            curr = next;
        }
    }
    
};

template<typename T>
struct Queue {
    
    struct Node {
        Atomic<Node const*> next;
        union { T payload; };
    };
    
    // false sharing
    Atomic<Node const*> head;
    Atomic<Node const*> tail;
    
    Queue()
    : Queue(new Node { nullptr }) {
    }

    explicit Queue(Node const* ptr)
    : head(ptr)
    , tail(ptr) {
    }
    
    void push(T x) {
        Node const* desired = new Node { nullptr, std::move(x) };
        Node const* tail = this->tail.load(std::memory_order_acquire);
        for (;;) {
            assert(tail);
            Node const* next = tail->next.load(std::memory_order_acquire);
            if (!next && tail->next.compare_exchange_strong(next,
                                                            desired,
                                                            std::memory_order_release,
                                                            std::memory_order_acquire)) {
                return;
            }
            assert(next);
            if (this->tail.compare_exchange_strong(tail,
                                                   next,
                                                   std::memory_order_release,
                                                   std::memory_order_acquire)) {
                tail = next;
            }
        }
    }
    
    std::optional<T> try_pop() {
        Node const* head = this->head.load(std::memory_order_acquire);
        for (;;) {
            assert(head);
            if (Node const* next = head->next.load(std::memory_order_acquire)) {
                if (this->head.compare_exchange_strong(head,
                                                       next,
                                                       std::memory_order_release,
                                                       std::memory_order_acquire)) {
                    Local::get().defer([=] { delete head; });
                    T tmp{std::move(next->payload)};
                    next->payload.~T();
                    return tmp;
                }
            } else {
                return std::optional<T>{};
            }
        }
    }
};



TEST_CASE("epoch") {
    
    std::size_t n = 0;
    
    std::vector<std::thread> t;
    for (int i = 0; i != n; ++i) {
        t.emplace_back([=] {
            
            Global const& global = Global::get();
            Local const& local = Local::get();
            
            for (;;) {

                // see crossbeam repin
                u64 local_epoch = local.epoch.load(std::memory_order_relaxed);
                u64 global_epoch = global.epoch.load(std::memory_order_relaxed);
                if (local_epoch != global_epoch) {
                    // unsafe for old memory accesses to leak into new epoch
                    local.epoch.store(global_epoch, std::memory_order_release);
                    // safe for new memory accesses to leak into old epoch
                }
                
                
                // we should advance "infrequently"
                // one defintion could be 1/nth of the time when n is number of
                // threads, since only one thread every epoch actually advances
                // it, and the others just overwrite the advance
                // see crossbeam try_advance
                bool flag = true;
                for (auto e : local.epochs()) {
                    if (e != local_epoch) {
                        flag = false;
                        break;
                    }
                }
                
                if (flag) {
                    // synchronize with other threads only when advancing
                    std::atomic_thread_fence(std::memory_order_acquire);
                    global.epoch.store(local_epoch + 1, std::memory_order_release);
                    printf("%d %llu\n", i, local_epoch);
                }
                
                // to sleep and wake we will need to implement crossbeam pin and unpin
                
            }
            
        });
    }
    
    
    while (!t.empty()) {
        t.back().join();
        t.pop_back();
    }
    
    
    /*
     return;
     
     std::size_t n = 8;
     
     const Atomic<u64> global{1000};
     std::vector<Atomic<u64>> locals;
     
     for (int i = 0; i != n; ++i)
     locals.emplace_back(0);
     
     const Atomic<const Atomic<int>*> p{new Atomic<int>{7}};
     
     std::vector<std::thread> t;
     for (int i = 0; i != n; ++i) {
     t.emplace_back([&global,
     &locals = std::as_const(locals),
     &p,
     i, n] {
     
     std::multimap<u64, const Atomic<int>*> bag;
     
     for (;;) {
     //std::this_thread::sleep_for(std::chrono::milliseconds{rand() % 1000});
     
     u64 epoch = global.load(std::memory_order_relaxed);
     //printf("%llx (%d)\n", epoch, i);
     blazes:
     locals[i].store(epoch, std::memory_order_relaxed);
     std::atomic_thread_fence(std::memory_order_seq_cst);
     u64 epoch2 = global.load(std::memory_order_relaxed);
     if (epoch2 != epoch) {
     epoch = epoch2;
     goto blazes;
     }
     
     
     
     
     if (rand() % 8) {
     p.load(std::memory_order_acquire)->fetch_add(1, std::memory_order_relaxed);
     } else {
     bag.emplace(epoch, p.exchange(new Atomic<int>{rand()}, std::memory_order_acq_rel));
     }
     
     bool flag = true;
     for (int j = 0; j != n; ++j) {
     auto z = locals[j].load(std::memory_order_relaxed);
     //assert(!z || (z == epoch) || (z == epoch + 1) || (z == epoch - 1));
     if (z && (z != epoch)) {
     flag = false;
     break;
     }
     }
     
     if (flag) {
     std::atomic_thread_fence(std::memory_order_acquire);
     global.store(epoch + 1, std::memory_order_release);
     // printf("%llx (%d)\n", epoch, i);
     }
     
     locals[i].store(0, std::memory_order_release);
     
     // bag?
     }
     });
     }
     
     
     
     
     
     while (!t.empty()) {
     t.back().join();
     t.pop_back();
     }
     
     */
}

