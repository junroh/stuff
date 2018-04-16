#pragma once

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

//#define USE_future
#ifdef USE_future
#include <future>
#define INIT_VAL(extsig,futsig)	{futsig = extsig.get_future();}
#define RET_VAL(extsig,futsig)											\
				do {													\
					if (futsig.wait_for(std::chrono::milliseconds(0))	\
								== std::future_status::timeout)			\
						return false;									\
					return true;										\
				}while(0)
#define SET_VAL(extsig)						\
				do{							\
					if(!TestDestroy())		\
						extsig.set_value();	\
				}while(0)
#else
#define INIT_VAL(extsig,futsig)	{extsig = false;}
#define RET_VAL(extsig,futsig)	{return extsig;}
#define SET_VAL(extsig)			{extsig = true;}
#define CLR_VAL(extsig)			{extsig = false;}
#endif


class StoppableThread
{
	typedef std::function<void(void)> pRunFunc;
public:
	StoppableThread() : m_thread(), m_exitSignal() {
		initInfo(std::bind(&StoppableThread::Entry, this));
	}
	StoppableThread(pRunFunc func) : m_thread(), m_exitSignal() {
		initInfo(func);
	}
	StoppableThread(pRunFunc func, pRunFunc cbfunc) : m_thread(), m_exitSignal() {
		initInfo(func);
		m_cbfunc = cbfunc;
	}
	virtual ~StoppableThread(){
		Delete();
		if (m_thread.joinable())
			m_thread.join();
	}
	bool Run(){
		if (IsRunning())
			return false;
		if (m_thread.joinable())
			m_thread.join();
		CLR_VAL(m_exitSignal);
		m_threadrunning = -1;
		std::function<void(void)> _threadfunc = [this](void) {
			m_threadrunning = 1;
			std::this_thread::yield();
			m_func();
			if (m_cbfunc && !TestDestroy())
				m_cbfunc();
			MarkEnd();
		};
		m_thread = std::thread(_threadfunc);
		while (m_threadrunning == -1)		// confirmation of starting thread
			std::this_thread::yield();
		return true;
	}
	void DeleteImmedRet() {
		SET_VAL(m_exitSignal);
	}
	void Delete(int millisec = 0) {
		WaitforFinish(millisec, true);
	}
	bool IsRunning(){
		return (m_threadrunning == 0) ? false : true;
	}
	void WaitforFinish(int millisec = 0, bool need_delete=false){
		std::unique_lock<std::mutex> lck(m_sigmutex);
		if (TestDestroy() || !IsRunning())
			return;
		m_isWait = true;
		if(need_delete)
			SET_VAL(m_exitSignal);
		if(millisec)
			m_sigcond.wait_for(lck,std::chrono::milliseconds(millisec));
		else
			m_sigcond.wait(lck);
	}
protected:
	bool TestDestroy(){
		RET_VAL(m_exitSignal, m_futureObj);
	}
	void MarkEnd() {
		std::unique_lock<std::mutex> lck(m_sigmutex);
		if (m_isWait){
			m_isWait = false;
			m_threadrunning = 0;
			lck.unlock();
			m_sigcond.notify_one();
		}
		else {
			m_threadrunning = 0;
		}
	}
	virtual void Entry() {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
private:
	void initInfo(pRunFunc func) {
		m_func = func;
		m_cbfunc = nullptr;
		m_isWait = false;
		m_threadrunning = 0;
		INIT_VAL(m_exitSignal, m_futureObj);
		SetThreadConfig();
	}
	pRunFunc			m_func;
	pRunFunc			m_cbfunc;
	std::thread			m_thread;
#ifdef USE_future
	std::promise<void>	m_exitSignal;
	std::future<void>	m_futureObj;
#else
	std::atomic<bool>	m_exitSignal;
#endif
	std::atomic<int>	m_threadrunning;
	
	bool					m_isWait;
	std::condition_variable	m_sigcond;
	std::mutex				m_sigmutex;
};
