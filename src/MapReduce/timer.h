#ifndef TIMER_H
#define TIMER_H
#include <functional>
#include <vector>
/**
 * @brief 时间轮定时器实现
 * 
 */
class Timer {
public:
    Timer(int rotation, int slot, std::function<void()> callback) 
        :m_rotation(rotation), m_slot(slot), m_callback(callback){}
    int getRotation() const { return m_rotation; }
    int getSlot() const { return m_slot; }
    void decreaseRotation() { -- m_rotation; }
    void active() { m_callback(); }

private:
    int m_rotation;
    int m_slot;
    std::function<void()> m_callback;
};

class TimerWheel {
public:
    TimerWheel(int slotNum)
        :m_slotNum(slotNum), m_currSlot(0), m_startTime(getCurrentMS()), m_timer(m_slotNum, std::vector<Timer*>()){}
    ~TimerWheel() {
        for (std::vector<Timer *> slot : m_timer) {
            for (Timer *timer : slot) {
                delete timer;
            }
        }
    }
    Timer* addTimer(unsigned long long timeoutMS, std::function<void()> callback) {
        if (timeoutMS < 0) return NULL;
        // 一圈 m_slotNum ms
        int rotations = timeoutMS / m_slotNum;
        int slot = (m_currSlot + (timeoutMS % m_slotNum)) % m_slotNum;
        Timer* timer = new Timer(rotations, slot, callback);
        m_timer[slot].push_back(timer);
        return timer;
    }
    void delTimer(Timer *timer) {
        if (!timer) return;
        for (auto it = m_timer[timer->getSlot()].begin(); it != m_timer[timer->getSlot()].end(); ++it) {
            Timer *curr = *it;
            if (curr == timer) {
                m_timer[timer->getSlot()].erase(it);

                delete curr;
                curr = NULL;
                break;
            }
        }
    }
    unsigned long long getCurrentMS() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
        return ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);
    }
    void tick() {
        /**
         * @brief 触发定时器
         * 
         */
        for (auto it = m_timer[m_currSlot].begin(); it != m_timer[m_currSlot].end();) {
            Timer *curr = *it;
            if (curr->getRotation() > 0) {
                curr->decreaseRotation();
                ++it;
            } else {
                curr->active();
                it = m_timer[m_currSlot].erase(it);
                delete curr;
                curr = NULL; 
            }
        }
        ++m_currSlot;
        m_currSlot = m_currSlot % m_slotNum;
    }
    void takeAllTimeout() {
        int now = getCurrentMS();
        int cnt = now - m_startTime;
        for (int i = 0; i < cnt; ++i) {
            tick();
        }
        m_startTime = now;
    }

private:
    int m_slotNum;
    int m_currSlot;
    unsigned long long m_startTime;
    std::vector<std::vector<Timer *>> m_timer;
};

#endif