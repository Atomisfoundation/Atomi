// Copyright 2018-2020 The Beam Team
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

#pragma once

#include "wallet/api/api.h"
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/api/i_atomic_swap_provider.h"
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
#include "wallet/core/wallet_db.h"

namespace beam::wallet
{
class ApiConnection : public IWalletApiHandler
{
public:
    struct IWalletData
    {
        virtual IWalletDB::Ptr getWalletDB() = 0;
        virtual Wallet& getWallet() = 0;
#ifdef BEAM_ATOMIC_SWAP_SUPPORT
        virtual const IAtomicSwapProvider& getAtomicSwapProvider() const = 0;
#endif  // BEAM_ATOMIC_SWAP_SUPPORT
    };
    ApiConnection(
        IWalletData& walletData
      , WalletApi::ACL acl
    );
    virtual ~ApiConnection();

    virtual void serializeMsg(const json& msg) = 0;

    template<typename T>
    void doResponse(const JsonRpcId& id, const T& response)
    {
        json msg;
        _api.getResponse(id, response, msg);
        serializeMsg(msg);
    }

    void doError(const JsonRpcId& id, ApiError code, const std::string& data = "");

    void onInvalidJsonRpc(const json& msg) override;

    void FillAddressData(const AddressData& data, WalletAddress& address);

#define MESSAGE_FUNC(api, name, _) \
    void onMessage(const JsonRpcId& id, const api& data) override;

    WALLET_API_METHODS(MESSAGE_FUNC)

#undef MESSAGE_FUNC

    void doTxAlreadyExistsError(const JsonRpcId& id);

    template<typename T>
    static void doPagination(size_t skip, size_t count, std::vector<T>& res)
    {
        if (count > 0)
        {
            size_t start = skip;
            size_t end = start + count;
            size_t size = res.size();

            if (start < size)
            {
                if (end > size) end = size;

                res = std::vector<T>(res.begin() + start, res.begin() + end);
            }
            else res = {};
        }
    }

protected:
    IWalletData& _walletData;
    WalletApi _api;
};
} // beam::wallet

