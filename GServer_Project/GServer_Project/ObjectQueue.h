#pragma once
#include <deque>
#include <mutex>

// -----------------------------------------------------------------------
// ObjectQueue
//   FSCore의 ObjectQueue 패턴을 참고한 스레드 안전 큐.
//   Push/Pop/Swap 연산을 recursive_mutex로 보호.
//   Swap을 이용해 워크 스레드에서 락 구간을 최소화한다.
// -----------------------------------------------------------------------
template<typename T>
class ObjectQueue
{
private:
    std::recursive_mutex mLock;
    std::deque<T>        mObjects;

public:
    void Clear()
    {
        std::lock_guard<std::recursive_mutex> guard(mLock);
        mObjects.clear();
    }

    size_t Count()
    {
        std::lock_guard<std::recursive_mutex> guard(mLock);
        return mObjects.size();
    }

    void Push(T object)
    {
        std::lock_guard<std::recursive_mutex> guard(mLock);
        mObjects.push_back(object);
    }

    T Pop()
    {
        T object = T();
        std::lock_guard<std::recursive_mutex> guard(mLock);
        if (!mObjects.empty())
        {
            object = mObjects.front();
            mObjects.pop_front();
        }
        return object;
    }

    // 내부 큐와 외부 deque를 swap — 락 구간을 짧게 유지하기 위해 사용
    void Swap(std::deque<T>& o_List)
    {
        std::lock_guard<std::recursive_mutex> guard(mLock);
        mObjects.swap(o_List);
    }
};
