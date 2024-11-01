#pragma once
#include <unistd.h>

#include <format>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

#if defined(__linux__)
#define USE_EPOLL
#include <sys/epoll.h>
#include <sys/timerfd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) ||    \
    defined(__NetBSD__)
#define USE_KQUEUE
#include <sys/event.h>
#else
#error "Neither kqueue nor epoll is supported on this platform."
#endif

#include "logging.hpp"

#define MAX_EVENTS 10

enum class EventType { READ, WRITE, TIMER, ERROR };
struct Event {
  int m_fd;
  EventType m_type;
  std::function<void(Event &)> *m_callback;
};
class EventLoop {
public:
  EventLoop() {
#if defined(USE_EPOLL)
    m_event_fd = epoll_create1(0);
    if (m_event_fd == -1) {
      std::cerr << "epoll_create1 failed" << std::endl;
      exit(1);
    }
#elif defined(USE_KQUEUE)
    m_event_fd = kqueue();
    if (m_event_fd == -1) {
      std::cerr << "kqueue failed" << std::endl;
      exit(1);
    }
#endif
  }
  // events is the pre-allocated loop
  // timeout is time in us
  int get_events(std::vector<Event> &events, int timeout) {
    int cnt = 0;
#if defined(USE_EPOLL)
    // this can't really figure out when a timer fires
    // rely on the callback for behavior
    struct epoll_event ev[MAX_EVENTS];
    cnt = epoll_wait(m_event_fd, ev, MAX_EVENTS, timeout / 1000);
    if (cnt == -1) {
      std::cerr << "epoll_wait failed" << std::endl;
      exit(1);
    }
    for (int i = 0; i < cnt; ++i) {
      Event &e = events[i];
      e.m_callback = nullptr;
      if (m_callbacks_fd.find(ev[i].data.fd) != m_callbacks_fd.end()) {
        e.m_callback = &m_callbacks_fd[ev[i].data.fd];
      }
      if (m_timers.find(ev[i].data.fd) != m_timers.end()) {
        // call read on the timer to reset it
        uint64_t exp;
        read(ev[i].data.fd, &exp, sizeof(exp));
        e.m_type = EventType::TIMER;
      }
      e.m_fd = ev[i].data.fd;
      if (ev[i].events & EPOLLIN) {
        e.m_type = EventType::READ;
      } else if (ev[i].events & EPOLLOUT) {
        e.m_type = EventType::WRITE;
      } else {
        e.m_type = EventType::ERROR;
      }
    }
#elif defined(USE_KQUEUE)
    struct kevent ev[MAX_EVENTS];
    struct timespec ts;
    ts.tv_sec = timeout / 1000000;
    ts.tv_nsec = (timeout % 1000000) * 1000;
    cnt = kevent(m_event_fd, nullptr, 0, ev, MAX_EVENTS, &ts);
    if (cnt == -1) {
      std::cerr << "kevent failed" << std::endl;
      exit(1);
    }
    for (int i = 0; i < cnt; ++i) {
      Event &e = events[i];
      e.m_callback = nullptr;
      e.m_fd = ev[i].ident;
      if (ev[i].filter == EVFILT_READ) {
        e.m_type = EventType::READ;
        e.m_callback = &m_callbacks_fd[ev[i].ident];
      } else if (ev[i].filter == EVFILT_WRITE) {
        e.m_type = EventType::WRITE;
        e.m_callback = &m_callbacks_fd[ev[i].ident];
      } else if (ev[i].filter == EVFILT_TIMER) {
        e.m_type = EventType::TIMER;
        e.m_callback = &m_callbacks_timer[ev[i].ident];
      } else {
        e.m_type = EventType::ERROR;
      }
    }
#endif
    return cnt;
  }
  void add_fd(
      int fd, EventType type,
      std::function<void(Event &)> callback = [](auto) {}) {
    m_callbacks_fd[fd] = callback;
#if defined(USE_EPOLL)
    struct epoll_event ev;
    ev.events = 0;
    if (type == EventType::READ) {
      ev.events |= EPOLLIN;
    } else if (type == EventType::WRITE) {
      ev.events |= EPOLLOUT;
    } else if (type == EventType::TIMER) {
      ev.events |= EPOLLIN;
    }
    ev.data.fd = fd;
    if (epoll_ctl(m_event_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
      std::cerr << "epoll_ctl failed" << std::endl;
      exit(1);
    }
#elif defined(USE_KQUEUE)
    struct kevent ev;
    EV_SET(&ev, fd, type == EventType::READ ? EVFILT_READ : EVFILT_WRITE,
           EV_ADD, 0, 0, nullptr);
    if (kevent(m_event_fd, &ev, 1, nullptr, 0, nullptr) == -1) {
      std::cerr << "kevent failed" << std::endl;
      exit(1);
    }
#endif
  }
  void remove_fd(int fd, EventType type) {
    if (m_callbacks_fd.find(fd) != m_callbacks_fd.end()) {
      m_callbacks_fd.erase(fd);
    }
#if defined(USE_EPOLL)
    if (epoll_ctl(m_event_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
      std::cerr << "epoll_ctl failed" << std::endl;
      exit(1);
    }
#elif defined(USE_KQUEUE)
    struct kevent ev;
    // don't try to delete a timer LOL
    EV_SET(&ev, fd, type == EventType::READ ? EVFILT_READ : EVFILT_WRITE,
           EV_DELETE, 0, 0, nullptr);
    if (kevent(m_event_fd, &ev, 1, nullptr, 0, nullptr) == -1) {
      std::cerr << "kevent failed" << std::endl;
      exit(1);
    }
#endif
  }

  // period in ms
  // you cannot remove timers LOL
  // the proper way to do this event loop is that a timer/fd returns a handle
  // number and we have a vector of handle callbacks. if we want to remove a
  // timer/fd we remove the callback and know the info to remove from the
  // epoll/kqueue
  //
  // however i don't want to worry about handles because we really don't need to
  // be accessing the handles externally for now - it's not *that* complex
  // it adds another layer of indirection, especially for reading from the
  // sockets and whatnot
  void add_timer(
      int period, std::function<void(Event &)> callback = [](auto) {}) {

#if defined(USE_EPOLL)
    struct itimerspec its;
    its.it_interval.tv_sec = period / 1000;
    its.it_interval.tv_nsec = (period % 1000) * 1000000;
    its.it_value.tv_sec = period / 1000;
    its.it_value.tv_nsec = (period % 1000) * 1000000;
    int fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (fd == -1) {
      std::cerr << "timerfd_create failed" << std::endl;
      exit(1);
    }
    if (timerfd_settime(fd, 0, &its, nullptr) == -1) {
      std::cerr << "timerfd_settime failed" << std::endl;
      exit(1);
    }
    m_timers.insert(fd);
    add_fd(fd, EventType::TIMER, callback);
#elif defined(USE_KQUEUE)
    m_callbacks_timer[m_timer_num] = callback;
    struct kevent ev;
    // crazy how NOTE_MSECONDS is not defined in the headers
    EV_SET(&ev, m_timer_num++, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, period,
           nullptr);
    if (kevent(m_event_fd, &ev, 1, nullptr, 0, nullptr) == -1) {
      std::cerr << "kevent failed" << std::endl;
      exit(1);
    }
#endif
  }

private:
  int m_event_fd;

  std::unordered_map<int, std::function<void(Event &)>> m_callbacks_fd;
  std::unordered_set<int> m_timers;
#if defined(USE_KQUEUE)
  int m_timer_num = 0;
  std::unordered_map<int, std::function<void(Event &)>> m_callbacks_timer;
#endif
};
