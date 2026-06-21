#pragma once
#include "Server.h"

// =======================================================================
// ExpOverPoolTagged
//   기존 ExpOverPool(Treiber Stack)의 ABA 문제를 해결한 버전.
//
// [기존 ExpOverPool의 문제]
//   atomic<Node*> 헤드를 CAS하는 동안 다른 스레드가 같은 노드를
//   pop→push 하면, 헤드 값(주소)이 A→B→A로 돌아와 CAS가 변화를
//   감지하지 못한다(ABA). IOCP에서는 같은 WSAOVERLAPPED가 두 워커에
//   배포되어 WSAENOTSOCK(10038) 등으로 이어질 수 있다.
//
// [해결: 인덱스 + 버전(Tagged) 방식]
//   고정 배열 풀이므로 "포인터"가 아니라 "배열 인덱스"를 다룬다.
//     - 하위 32bit : 노드 인덱스
//     - 상위 32bit : 버전(tag) — push/pop 할 때마다 +1
//   둘을 64bit 하나에 패킹해 단일 64bit CAS로 갱신한다.
//   인덱스 A가 똑같이 돌아와도 tag가 달라 64bit 전체 값이 절대
//   같아지지 않으므로 CAS가 변화를 잡아낸다 → ABA 원천 차단.
//
//   * x64에서 atomic<uint64_t>는 네이티브 lock cmpxchg → is_lock_free() == true
//   * new/delete 0회(풀의 목적) 그대로 유지
//
// [인터페이스]
//   기존 ExpOverPool과 동일: Init / Acquire / Release / AvailableCount
//   → g_ExpOverPool 사용처를 그대로 둔 채 타입만 바꿔 교체 가능.
// =======================================================================
class ExpOverPoolTagged
{
private:
    // 스택 노드: EXP_OVER 인라인 포함 + "다음 노드 인덱스"
    struct Node {
        EXP_OVER over;
        uint32_t nextIndex;   // Node* 대신 인덱스 (NULL_IDX = 끝)
    };

    static constexpr uint32_t NULL_IDX = 0xFFFFFFFFu;

    std::atomic<uint64_t> mHead{ 0 };   // [상위32: tag][하위32: index]
    std::vector<Node>     mNodes;       // 실제 메모리 (한 번만 할당)

    // --- 패킹/언패킹 ---
    static uint64_t pack(uint32_t idx, uint32_t tag) {
        return (static_cast<uint64_t>(tag) << 32) | idx;
    }
    static uint32_t idx_of(uint64_t h) { return static_cast<uint32_t>(h & 0xFFFFFFFFu); }
    static uint32_t tag_of(uint64_t h) { return static_cast<uint32_t>(h >> 32); }

public:
    const char* Name() const { return "Tagged(index+version)"; }

    // -------------------------------------------------------------------
    // Init — 서버 시작 시 1회. poolSize개 노드를 인덱스 연결 리스트로 구성.
    // -------------------------------------------------------------------
    void Init(size_t poolSize)
    {
        mNodes.resize(poolSize);
        for (uint32_t i = 0; i < poolSize - 1; ++i)
            mNodes[i].nextIndex = i + 1;
        mNodes[poolSize - 1].nextIndex = NULL_IDX;

        mHead.store(pack(0, 0), std::memory_order_relaxed);  // head = index 0, tag 0
        std::cout << "[ExpOverPoolTagged] Init: " << poolSize << " blocks, "
                  << (poolSize * sizeof(Node) / 1024) << " KB, lock_free="
                  << mHead.is_lock_free() << "\n";
    }

    // -------------------------------------------------------------------
    // Acquire — pop. O(1), lock-free, ABA-safe.
    //   풀 고갈 시 new 로 fallback.
    // -------------------------------------------------------------------
    EXP_OVER* Acquire()
    {
        uint64_t oldHead = mHead.load(std::memory_order_acquire);
        for (;;) {
            uint32_t idx = idx_of(oldHead);
            if (idx == NULL_IDX) {
                std::cout << "[ExpOverPoolTagged] WARN: pool exhausted, fallback new\n";
                return new EXP_OVER();
            }
            uint32_t nextIdx = mNodes[idx].nextIndex;

            // ★ 꺼낼 때마다 tag +1 → ABA 차단
            uint64_t newHead = pack(nextIdx, tag_of(oldHead) + 1);

            if (mHead.compare_exchange_weak(
                    oldHead, newHead,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                return &mNodes[idx].over;
            }
            // 실패 시 oldHead 자동 갱신 → 재시도
        }
    }

    // -------------------------------------------------------------------
    // Release — push. O(1), lock-free, ABA-safe.
    //   풀 범위 밖(fallback new) 포인터는 delete.
    // -------------------------------------------------------------------
    void Release(EXP_OVER* pOver)
    {
        if (pOver == nullptr) return;

        Node* pNode = reinterpret_cast<Node*>(
            reinterpret_cast<char*>(pOver) - offsetof(Node, over));

        bool isInPool = (pNode >= mNodes.data()) &&
                        (pNode <  mNodes.data() + mNodes.size());
        if (!isInPool) { delete pOver; return; }

        uint32_t idx = static_cast<uint32_t>(pNode - mNodes.data());

        uint64_t oldHead = mHead.load(std::memory_order_relaxed);
        for (;;) {
            pNode->nextIndex = idx_of(oldHead);

            // ★ 넣을 때도 tag +1
            uint64_t newHead = pack(idx, tag_of(oldHead) + 1);

            if (mHead.compare_exchange_weak(
                    oldHead, newHead,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
                break;
            }
        }
    }

    // 현재 풀에 남은 블록 수 (디버그용 — 동시 접근 중에는 근사값)
    size_t AvailableCount() const
    {
        size_t count = 0;
        uint32_t cur = idx_of(mHead.load(std::memory_order_relaxed));
        while (cur != NULL_IDX) {
            ++count;
            cur = mNodes[cur].nextIndex;
        }
        return count;
    }
};
