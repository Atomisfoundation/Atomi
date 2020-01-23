// Copyright 2019 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "private_key_keeper.h"

namespace beam::wallet
{

	////////////////////////////////
	// Synchronous methods implemented via asynchronous
	struct IPrivateKeyKeeper2::HandlerSync
		:public Handler
	{
		Status::Type m_Status = Status::InProgress;

		virtual void OnDone(Status::Type nRes) override
		{
			m_Status = nRes;
			io::Reactor::get_Current().stop();
		}

		Status::Type Wait()
		{
			io::Reactor::get_Current().run();
			return m_Status;
		}
	};


	template <typename TMethod>
	IPrivateKeyKeeper2::Status::Type IPrivateKeyKeeper2::InvokeSyncInternal(TMethod& m)
	{
		struct MyHandler
			:public HandlerSync
		{
			TMethod m_M;
			virtual ~MyHandler() {}
		};

 		std::shared_ptr<MyHandler> p(new MyHandler);
		p->m_M = std::move(m);

		InvokeAsync(p->m_M, p);
		Status::Type ret = p->Wait();

		if (Status::InProgress != ret)
			m = std::move(p->m_M);

		return ret;

	}

#define THE_MACRO(method) \
	IPrivateKeyKeeper2::Status::Type IPrivateKeyKeeper2::InvokeSync(Method::method& m) \
	{ \
		return InvokeSyncInternal(m); \
	}

	KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

	////////////////////////////////
	// Asynchronous methods implemented via synchronous
#define THE_MACRO(method) \
	void IPrivateKeyKeeper2::InvokeAsync(Method::method& m, const Handler::Ptr& p) \
	{ \
		Status::Type res = InvokeSync(m); \
		p->OnDone(res); \
	}

	KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO

	////////////////////////////////
	// misc
	void IPrivateKeyKeeper2::Method::KernelCommon::To(TxKernelStd& krn) const
	{
		krn.m_Commitment = m_Commitment;
		krn.m_Fee = m_Fee;
		krn.m_Height = m_Height;
		krn.m_Signature = m_Signature;
	}

	void IPrivateKeyKeeper2::Method::KernelCommon::From(const TxKernelStd& krn)
	{
		m_Commitment = krn.m_Commitment;
		m_Fee = krn.m_Fee;
		m_Height = krn.m_Height;
		m_Signature = krn.m_Signature;
	}

	////////////////////////////////
	// ThreadedPrivateKeyKeeper
	void ThreadedPrivateKeyKeeper::TaskList::Pop(Task::Ptr& p)
	{
		assert(!empty());
		Task& t = front();

		erase(TaskList::s_iterator_to(t));
		p.reset(&t);
	}

	bool ThreadedPrivateKeyKeeper::TaskList::Push(Task::Ptr& p) // returns if was empty
	{
		bool b = empty();
		push_back(*p.release());
		return b;
	}

	void ThreadedPrivateKeyKeeper::TaskList::Clear()
	{
		while (!empty())
		{
			Task::Ptr p;
			Pop(p);
		}
	}

	void ThreadedPrivateKeyKeeper::PushIn(Task::Ptr& p)
	{
		std::unique_lock<std::mutex> scope(m_MutexIn);

		if (m_queIn.Push(p))
			m_NewIn.notify_one();
	}

	void ThreadedPrivateKeyKeeper::Thread()
	{
		while (true)
		{
			Task::Ptr pTask;

			{
				std::unique_lock<std::mutex> scope(m_MutexIn);
				while (true)
				{
					if (!m_Run)
						return;

					if (!m_queIn.empty())
					{
						m_queIn.Pop(pTask);
						break;
					}

					m_NewIn.wait(scope);
				}
			}

			assert(pTask);
			pTask->Exec(*m_pKeyKeeper);

			{
				std::unique_lock<std::mutex> scope(m_MutexOut);

				if (m_queOut.Push(pTask))
					m_pNewOut->post();
			}
		}
	}

	void ThreadedPrivateKeyKeeper::OnNewOut()
	{
		// protect this obj from destruction from the handler invocation
		TaskList que;
		{
			std::unique_lock<std::mutex> scope(m_MutexOut);
			m_queOut.swap(que);
		}

		while (!que.empty())
		{
			Task::Ptr pTask;
			que.Pop(pTask);
			pTask->m_pHandler->OnDone(pTask->m_Status);
		}
	}

	ThreadedPrivateKeyKeeper::ThreadedPrivateKeyKeeper(const IPrivateKeyKeeper2::Ptr& p)
		:m_pKeyKeeper(p)
	{
		io::AsyncEvent::Callback cb = [this]() { OnNewOut(); };
		m_pNewOut = io::AsyncEvent::create(io::Reactor::get_Current(), std::move(cb));

		m_Thread = std::thread(&ThreadedPrivateKeyKeeper::Thread, this);
	}

	ThreadedPrivateKeyKeeper::~ThreadedPrivateKeyKeeper()
	{
		if (m_Thread.joinable())
		{
			{
				std::unique_lock<std::mutex> scope(m_MutexIn);
				m_Run = false;
				m_NewIn.notify_one();
			}
			m_Thread.join();
		}
	}

	template <typename TMethod>
	void ThreadedPrivateKeyKeeper::InvokeAsyncInternal(TMethod& m, const Handler::Ptr& pHandler)
	{
		struct MyTask :public Task {
			TMethod* m_pM;
			virtual void Exec(IPrivateKeyKeeper2& k) override { m_Status = k.InvokeSync(*m_pM); }
		};

		Task::Ptr pTask(new MyTask);
		pTask->m_pHandler = pHandler;
		Cast::Up<MyTask>(*pTask).m_pM = &m;

		PushIn(pTask);
	}

#define THE_MACRO(method) \
	void ThreadedPrivateKeyKeeper::InvokeAsync(Method::method& m, const Handler::Ptr& pHandler) \
	{ \
		InvokeAsyncInternal<Method::method>(m, pHandler); \
	}

	KEY_KEEPER_METHODS(THE_MACRO)
#undef THE_MACRO



} // namespace beam::wallet