#pragma once
#include "Server.h"

// -----------------------------------------------------------------------
// ExpOverPool
// EXP_OVER 객체를 미리 할당해두고 Acquire/Release로 재사용하는 풀.
//
// 구조: Lock-free Treiber Stack
//   - atomic<Node*> 로 CAS 연산만 사용 → mutex 없음
//   - 16개 IOCP worker가 동시에 Acquire/Release해도 안전
//
// 사용법:
//   EXP_OVER* p = g_ExpOverPool.Acquire();
//   ZeroMemory(&p->_wsa_over, sizeof(p->_wsa_over));  // 반드시!
//   // ... 필드 직접 초기화 ...
//   g_ExpOverPool.Release(p);
//
// 주의:
//   - Acquire 후 반드시 _wsa_over를 ZeroMemory할 것 (IOCP가 직접 참조)
//   - new/delete를 절대 쓰지 말 것 (풀 밖으로 나가면 누수)
// -----------------------------------------------------------------------
class ExpOverPool
{
private:
    // 스택 노드: EXP_OVER를 인라인으로 포함, next 포인터만 추가
    struct Node {
        EXP_OVER over;
        Node*    next = nullptr;
    };

    std::atomic<Node*> mHead{ nullptr };  // 스택 헤드 (CAS 대상)
    std::vector<Node>  mNodes;         // 실제 메모리 블록 (한 번만 할당)

public:
    // -----------------------------------------------------------------------
    // Init
    // 서버 시작 시 1회 호출. poolSize개의 Node를 연결 리스트로 구성.
    // poolSize 권장값: MAX_USER × 시야내최대인원 × 2
    // 예) 200 × 30 × 2 = 12,000
    // -----------------------------------------------------------------------
    void Init(size_t poolSize)
    {
        mNodes.resize(poolSize);
        for (size_t i = 0; i < poolSize - 1; ++i)
            mNodes[i].next = &mNodes[i + 1];
        mNodes[poolSize - 1].next = nullptr;
        mHead.store(&mNodes[0], std::memory_order_relaxed);
        std::cout << "[ExpOverPool] Init: " << poolSize << " blocks, "
                  << (poolSize * sizeof(Node) / 1024) << " KB\n";
    }

    // -----------------------------------------------------------------------
    // Acquire
    // 풀에서 EXP_OVER 하나를 꺼냄. O(1), lock-free.
    // 풀이 고갈되면 new로 fallback (경고 출력).
    // -----------------------------------------------------------------------
    EXP_OVER* Acquire()
    {
        Node* oldTop = mHead.load(std::memory_order_acquire);
        while (oldTop != nullptr) {
            Node* newTop = oldTop->next;
            if (mHead.compare_exchange_weak(
                    oldTop, newTop,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                oldTop->next = nullptr;
                return &oldTop->over;
            }
        }
        std::cout << "[ExpOverPool] WARN: pool exhausted, fallback new\n";
        return new EXP_OVER();
    }

    // -----------------------------------------------------------------------
    // Release
    // 사용 끝난 EXP_OVER를 풀에 반납. O(1), lock-free.
    // fallback new로 만들어진 포인터(풀 범위 밖)는 delete로 처리.
    // -----------------------------------------------------------------------
    void Release(EXP_OVER* pOver)
    {
        if (pOver == nullptr) return;

        Node* pNode = reinterpret_cast<Node*>(
            reinterpret_cast<char*>(pOver) - offsetof(Node, over));
        bool isInPool = (pNode >= mNodes.data()) &&
                        (pNode <  mNodes.data() + mNodes.size());

        if (!isInPool) {
            delete pOver;
            return;
        }

        Node* oldTop = mHead.load(std::memory_order_relaxed);
        do {
            pNode->next = oldTop;
        } while (!mHead.compare_exchange_weak(
                     oldTop, pNode,
                     std::memory_order_release,
                     std::memory_order_relaxed));
    }

    // 현재 풀에 남은 블록 수 (디버그용)
    size_t AvailableCount() const
    {
        size_t count = 0;
        Node* cur = mHead.load(std::memory_order_relaxed);
        while (cur) { ++count; cur = cur->next; }
        return count;
    }
};

// 전역 싱글턴 선언 — 정의는 ExpOverPool.cpp
extern ExpOverPool g_ExpOverPool;
