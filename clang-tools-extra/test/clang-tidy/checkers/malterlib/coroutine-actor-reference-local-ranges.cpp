// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-actor-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

// A loop over what the coroutine holds itself, a value an await gives, a
// local array or collection, also a member of a local held by value, or a
// list made where the loop is, goes on over nothing of the actor, though
// what it holds can be made of the actor's data.

namespace std
{
	template <typename t_CValue>
	class initializer_list
	{
		t_CValue const *m_pBegin;
		decltype(sizeof(0)) m_Size;

	public:
		t_CValue const *begin() const;
		t_CValue const *end() const;
	};
}

namespace NLocalRanges
{
	TCFuture<int> fg_Work();

	template <typename t_CValue>
	struct TCList
	{
		t_CValue *m_pData = nullptr;

		t_CValue *begin();
		t_CValue *end();
		void f_Remove(int _Key);
		void f_Clear();
	};

	struct CSensorInfo
	{
		int m_Value = 0;
	};

	struct CPipelinedIterator
	{
		void *m_pState = nullptr;

		TCList<CSensorInfo> &operator*();
		CPipelinedIterator &operator++();
	};

	struct CSensorsGenerator
	{
		void *m_pState = nullptr;

		TCFuture<CPipelinedIterator> f_GetPipelinedIterator() &&;
	};

	struct CAsset
	{
		int m_Value = 0;
		int *m_pData = nullptr;
	};

	struct CRelease
	{
		TCList<CAsset> m_Assets;
	};

	struct CListenAddress
	{
		int *m_pPort = nullptr;
		int m_Value = 0;
	};

	// A copy made by a free function, which owns what it holds.
	template <typename t_CValue>
	TCList<t_CValue> fg_TempCopy(TCList<t_CValue> const &_Values);

	// Debug managers, by a handle of another actor and plain data about it.
	struct CDebugManagerHandle
	{
		void *m_pActor = nullptr;

		TCFuture<int> f_Delete();
	};

	struct CTrustInfo
	{
		int m_Port = 0;
	};

	struct CTrustedActor
	{
		CDebugManagerHandle m_Actor;
		CTrustInfo m_TrustInfo;
	};

	struct CDebugManagers
	{
		TCList<CTrustedActor> m_Actors;
	};

	// A wrapper that refers to a collection it doesn't own.
	struct CAssetsRef
	{
		TCList<CAsset> &m_Items;

		CAsset *begin();
		CAsset *end();
		void f_Clear();
	};

	struct CSocket
	{
		TCFuture<int> f_Close();
	};

	struct CApp : CActor
	{
		TCList<CSensorInfo> mp_Sensors;
		TCList<CAsset> mp_Assets;
		TCList<int> mp_Values;
		CDebugManagers mp_DebugManagers;
		int mp_Port = 0;
		CSocket m_Client;
		CSocket m_UDP;
		CSocket m_Listen;

		TCFuture<CSensorsGenerator> fp_GetSensors(TCList<CSensorInfo> &_Sensors);
		TCFuture<CRelease> fp_GetOrCreateRelease(TCList<CAsset> &_Assets);

		// Over what a pipelined iterator of a moved local generator gives.
		TCFuture<int> f_Sensors()
		{
			auto SensorsGenerator = co_await fp_GetSensors(mp_Sensors);
			auto iSensors = co_await fg_Move(SensorsGenerator).f_GetPipelinedIterator();
			int Total = 0;
			for (auto &SensorInfo : *iSensors)
			{
				co_await fg_Work();
				Total += SensorInfo.m_Value;
			}
			co_return Total;
		}

		// Over a local array of values made of the actor's data.
		TCFuture<int> f_ListenAddresses()
		{
			CListenAddress ListenAddresses[2] = {{&mp_Port, 1}, {&mp_Port, 2}};
			int Total = 0;
			for (auto &ListenAddress : ListenAddresses)
			{
				co_await fg_Work();
				Total += ListenAddress.m_Value;
			}
			co_return Total;
		}

		// Over a collection held by a value an await gives.
		TCFuture<int> f_Release()
		{
			auto Release = co_await fp_GetOrCreateRelease(mp_Assets);
			int Total = 0;
			for (auto &OldAsset : Release.m_Assets)
			{
				co_await fg_Work();
				Total += OldAsset.m_Value;
			}
			co_return Total;
		}

		// Over a list of the addresses of the actor's own members.
		TCFuture<int> f_CloseSockets()
		{
			for (auto *pSocket : {&m_Client, &m_UDP, &m_Listen})
				co_await pSocket->f_Close();
			co_return 0;
		}

		// Over an owning copy a free function gives.
		TCFuture<int> f_TempCopy()
		{
			int Total = 0;
			for (auto Value : fg_TempCopy(mp_Values))
			{
				co_await fg_Work();
				Total += Value;
			}
			co_return Total;
		}

		// Over the entries of a local copy, which are the copy's own.
		TCFuture<int> f_DeleteAssets()
		{
			// A copy: the debug managers can come and go while the deletes are awaited
			auto DebugManagers = mp_DebugManagers.m_Actors;
			int Total = 0;
			for (auto &TrustedActor : DebugManagers)
			{
				auto DeleteResult = co_await TrustedActor.m_Actor.f_Delete();
				Total += DeleteResult + TrustedActor.m_TrustInfo.m_Port;
			}
			co_return Total;
		}

		CAssetsRef f_AssetsRef()
		{
			return {mp_Assets};
		}

		// But a local wrapper of a collection of the actor is the actor's data,
		// whatever its member functions are named.
		TCFuture<int> f_LocalWrapper()
		{
			auto Entries = f_AssetsRef();
			int Total = 0;
			for (auto &Asset : Entries)
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the loop goes on over the data of the actor after a suspension, during which other calls of the actor can change or erase the data; loop over a copy, or look the entry up again after the suspension [malterlib-coroutine-actor-reference]
			{
				co_await fg_Work();
				Total += Asset.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'Asset' refers into the data of the actor and is used after a suspension, {{.*}}
			}
			co_return Total;
		}

		// But a collection of the actor is the actor's data.
		TCFuture<int> f_Assets()
		{
			int Total = 0;
			for (auto &Asset : mp_Assets)
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the loop goes on over the data of the actor after a suspension, during which other calls of the actor can change or erase the data; loop over a copy, or look the entry up again after the suspension [malterlib-coroutine-actor-reference]
			{
				co_await fg_Work();
				Total += Asset.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'Asset' refers into the data of the actor and is used after a suspension, {{.*}}
			}
			co_return Total;
		}
	};
}
