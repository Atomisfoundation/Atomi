// Copyright 2018 The Beam Team
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

#include "simple_transaction.h"

#include "base_tx_builder.h"
#include "wallet.h"
#include "core/block_crypt.h"
#include "strings_resources.h"

#include <numeric>
#include "utility/logger.h"

namespace beam::wallet
{
    using namespace ECC;
    using namespace std;

    TxParameters CreateSimpleTransactionParameters(const boost::optional<TxID>& txId)
    {
        return CreateTransactionParameters(TxType::Simple, txId).SetParameter(TxParameterID::TransactionType, TxType::Simple);
    }

    TxParameters CreateSplitTransactionParameters(const WalletID& myID, const AmountList& amountList, const boost::optional<TxID>& txId)
    {
        return CreateSimpleTransactionParameters(txId)
            .SetParameter(TxParameterID::MyID, myID)
            .SetParameter(TxParameterID::PeerID, myID)
            .SetParameter(TxParameterID::AmountList, amountList)
            .SetParameter(TxParameterID::Amount, std::accumulate(amountList.begin(), amountList.end(), Amount(0)));
    }

    SimpleTransaction::Creator::Creator(IWalletDB::Ptr walletDB)
        : m_WalletDB(walletDB)
    {

    }

    BaseTransaction::Ptr SimpleTransaction::Creator::Create(INegotiatorGateway& gateway
                                                          , IWalletDB::Ptr walletDB
                                                          , IPrivateKeyKeeper::Ptr keyKeeper
                                                          , const TxID& txID)
    {
        return BaseTransaction::Ptr(new SimpleTransaction(gateway, walletDB, keyKeeper, txID));
    }

    TxParameters SimpleTransaction::Creator::CheckAndCompleteParameters(const TxParameters& parameters)
    {
        auto peerID = parameters.GetParameter<WalletID>(TxParameterID::PeerID);
        if (!peerID)
        {
            throw InvalidTransactionParametersException("");
        }
        auto receiverAddr = m_WalletDB->getAddress(*peerID);
        if (receiverAddr)
        {
            if (receiverAddr->isOwn() && receiverAddr->isExpired())
            {
                LOG_INFO() << "Can't send to the expired address.";
                throw AddressExpiredException();
            }

            // update address comment if changed
            if (auto message = parameters.GetParameter(TxParameterID::Message); message)
            {
                auto messageStr = std::string(message->begin(), message->end());
                if (messageStr != receiverAddr->m_label)
                {
                    receiverAddr->m_label = messageStr;
                    m_WalletDB->saveAddress(*receiverAddr);
                }
            }          

            TxParameters temp{ parameters };
            temp.SetParameter(TxParameterID::IsSelfTx, receiverAddr->isOwn());
            return temp;
        }
        else
        {
            WalletAddress address;
            address.m_walletID = *peerID;
            address.m_createTime = getTimestamp();
            if (auto message = parameters.GetParameter(TxParameterID::Message); message)
            {
                address.m_label = std::string(message->begin(), message->end());
            }

            m_WalletDB->saveAddress(address);
        }
        return parameters;
    }

    SimpleTransaction::SimpleTransaction(INegotiatorGateway& gateway
                                       , IWalletDB::Ptr walletDB
                                       , IPrivateKeyKeeper::Ptr keyKeeper
                                       , const TxID& txID)
        : BaseTransaction{ gateway, walletDB, keyKeeper, txID }
    {

    }

    TxType SimpleTransaction::GetType() const
    {
        return TxType::Simple;
    }

    bool SimpleTransaction::IsInSafety() const
    {
        State txState = GetState();
        return txState == State::KernelConfirmation;
    }

    void SimpleTransaction::UpdateImpl()
    {
        bool isSender = GetMandatoryParameter<bool>(TxParameterID::IsSender);
        bool isSelfTx = IsSelfTx();
        State txState = GetState();
        AmountList amoutList;
        if (!GetParameter(TxParameterID::AmountList, amoutList))
        {
            amoutList = AmountList{ GetMandatoryParameter<Amount>(TxParameterID::Amount) };
        }

        if (!m_TxBuilder)
        {
            m_TxBuilder = make_shared<BaseTxBuilder>(*this, kDefaultSubTxID, amoutList, GetMandatoryParameter<Amount>(TxParameterID::Fee));
        }
        auto sharedBuilder = m_TxBuilder;
        BaseTxBuilder& builder = *sharedBuilder;

        builder.GetPeerInputsAndOutputs();

        // Check if we already have signed kernel
        if ((isSender && !builder.LoadKernel())
         || (!isSender && (!builder.HasKernelID() || txState == State::Initial)))
        {
            // We don't need key keeper initialized to go on beyond this point
            if (!m_KeyKeeper)
            {
                // public wallet
                return;
            }

            if (!builder.GetInitialTxParams() && txState == State::Initial)
            {
                const auto isAsset = builder.GetAssetId() != 0;
                PeerID myWalletID, peerWalletID;
                bool hasID = GetParameter<PeerID>(TxParameterID::MySecureWalletID, myWalletID)
                    && GetParameter<PeerID>(TxParameterID::PeerSecureWalletID, peerWalletID);
                stringstream ss;
                ss << GetTxID() << (isSender ? " Sending " : " Receiving ")
                    << PrintableAmount(builder.GetAmount(), false,isAsset ? kAmountASSET : "", isAsset ? kAmountAGROTH : "")
                    << " (fee: " << PrintableAmount(builder.GetFee()) << ")";

                if (hasID)
                {
                    ss << " my ID: " << myWalletID << ", peer ID: " << peerWalletID;
                }
                LOG_INFO() << ss.str();

                UpdateTxDescription(TxStatus::InProgress);

                if (isSender)
                {
                    Height maxResponseHeight = 0;
                    if (GetParameter(TxParameterID::PeerResponseHeight, maxResponseHeight))
                    {
                        LOG_INFO() << GetTxID() << " Max height for response: " << maxResponseHeight;
                    }

                    builder.SelectInputs();
                    builder.AddChange();
                    builder.GenerateNonce();
                }

                if (isSelfTx || !isSender)
                {
                    // create receiver utxo
                    for (const auto& amount : builder.GetAmountList())
                    {
                        if (builder.GetAssetId() != 0)
                        {
                            builder.GenerateAssetCoin(amount, false);
                        }
                        else
                        {
                            builder.GenerateBeamCoin(amount, false);
                        }
                    }
                }
            }

            if (builder.CreateInputs())
            {
                return;
            }

            if (builder.CreateOutputs())
            {
                return;
            }

            if (!isSelfTx && !builder.GetPeerPublicExcessAndNonce())
            {
                assert(IsInitiator());
                if (txState == State::Initial)
                {
                    if (builder.SignSender(true))
                        return;

                    SendInvitation(builder, isSender);
                    SetState(State::Invitation);
                }
                UpdateOnNextTip();
                return;
            }

            if (!builder.UpdateMaxHeight())
            {
                OnFailed(TxFailureReason::MaxHeightIsUnacceptable, true);
                return;
            }

            builder.CreateKernel();

            if (!isSelfTx && !builder.GetPeerSignature())
            {
                if (txState == State::Initial)
                {
                    // invited participant
                    assert(!IsInitiator());

                    if (builder.SignReceiver())
                        return;

                    UpdateTxDescription(TxStatus::Registering);
                    ConfirmInvitation(builder);

                    uint32_t nVer = 0;
                    if (GetParameter(TxParameterID::PeerProtoVersion, nVer))
                    {
                        // for peers with new flow, we assume that after we have responded, we have to switch to the state of awaiting for proofs
						uint8_t nCode = proto::TxStatus::Ok; // compiler workaround (ref to static const)
						SetParameter(TxParameterID::TransactionRegistered, nCode);

                        SetState(State::KernelConfirmation);
                        ConfirmKernel(builder.GetKernelID());
                    }
                    else
                    {
                        SetState(State::InvitationConfirmation);
                    }
                    return;
                }
                if (IsInitiator())
                {
                    return;
                }
            }

            if (!isSelfTx)
            {
                if (builder.SignSender(false))
                    return;
            }
            else
            {
                if (builder.SignReceiver())
                    return;
            }

            if (IsInitiator() && !builder.IsPeerSignatureValid())
            {
                OnFailed(TxFailureReason::InvalidPeerSignature, true);
                return;
            }

            builder.FinalizeSignature();
        }

        uint8_t nRegistered = proto::TxStatus::Unspecified;
        if (!GetParameter(TxParameterID::TransactionRegistered, nRegistered))
        {
            if (CheckExpired())
            {
                return;
            }

            // Construct transaction
            auto transaction = builder.CreateTransaction();

            // Verify final transaction
            TxBase::Context::Params pars;
			TxBase::Context ctx(pars);
			ctx.m_Height.m_Min = builder.GetMinHeight();
			if (!transaction->IsValid(ctx))
            {
                OnFailed(TxFailureReason::InvalidTransaction, true);
                return;
            }
            GetGateway().register_tx(GetTxID(), transaction);
            SetState(State::Registration);
            return;
        }
        
        if (proto::TxStatus::InvalidContext == nRegistered) // we have to ensure that this transaction hasn't already added to blockchain)
        {
            Height lastUnconfirmedHeight = 0;
            if (GetParameter(TxParameterID::KernelUnconfirmedHeight, lastUnconfirmedHeight) && lastUnconfirmedHeight > 0)
            {
                OnFailed(TxFailureReason::FailedToRegister, true);
                return;
            }
        }
        else if (proto::TxStatus::Ok != nRegistered )
        {
            OnFailed(TxFailureReason::FailedToRegister, true);
            return;
        }

        Height hProof = 0;
        GetParameter(TxParameterID::KernelProofHeight, hProof);
        if (!hProof)
        {
            SetState(State::KernelConfirmation);
            ConfirmKernel(builder.GetKernelID());
            return;
        }

        SetCompletedTxCoinStatuses(hProof);

        CompleteTx();
    }

    void SimpleTransaction::SendInvitation(const BaseTxBuilder& builder, bool isSender)
    {
        TxParameters params;
        params.SetParameter(TxParameterID::Amount, builder.GetAmount())
              .SetParameter(TxParameterID::Fee, builder.GetFee())
              .SetParameter(TxParameterID::MinHeight, builder.GetMinHeight())
              .SetParameter(TxParameterID::Lifetime, builder.GetLifetime())
              .SetParameter(TxParameterID::PeerMaxHeight, builder.GetMaxHeight())
              .SetParameter(TxParameterID::IsSender, !isSender)
              .SetParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
              .SetParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
              .SetParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
              .SetParameter(TxParameterID::AssetID, builder.GetAssetId());

        if (!SendTxParameters(move(params)))
        {
            OnFailed(TxFailureReason::FailedToSendParameters, false);
        }
    }

    void SimpleTransaction::ConfirmInvitation(const BaseTxBuilder& builder)
    {
        LOG_INFO() << GetTxID() << " Transaction accepted. Kernel: " << builder.GetKernelIDString();
        TxParameters params;
        params
            .SetParameter(TxParameterID::PeerProtoVersion, s_ProtoVersion)
            .SetParameter(TxParameterID::PeerPublicExcess, builder.GetPublicExcess())
            .SetParameter(TxParameterID::PeerSignature, builder.GetPartialSignature())
            .SetParameter(TxParameterID::PeerPublicNonce, builder.GetPublicNonce())
            .SetParameter(TxParameterID::PeerMaxHeight, builder.GetMaxHeight())
            .SetParameter(TxParameterID::PeerInputs, builder.GetInputs())
            .SetParameter(TxParameterID::PeerOutputs, builder.GetOutputs())
            .SetParameter(TxParameterID::PeerOffset, builder.GetOffset());

        assert(!IsSelfTx());
        if (!GetMandatoryParameter<bool>(TxParameterID::IsSender))
        {
            Signature paymentProofSignature;
            if (GetParameter(TxParameterID::PaymentConfirmation, paymentProofSignature))
            {
                params.SetParameter(TxParameterID::PaymentConfirmation, paymentProofSignature);
            }
            else
            {
                PaymentConfirmation pc;
                WalletID widPeer, widMy;
                bool bSuccess =
                    GetParameter(TxParameterID::PeerID, widPeer) &&
                    GetParameter(TxParameterID::MyID, widMy) &&
                    GetParameter(TxParameterID::KernelID, pc.m_KernelID) &&
                    GetParameter(TxParameterID::Amount, pc.m_Value);

                if (bSuccess)
                {
                    pc.m_Sender = widPeer.m_Pk;

                    auto waddr = m_WalletDB->getAddress(widMy);
                    if (waddr && waddr->isOwn())
                    {
                        Scalar::Native sk;

                        m_KeyKeeper->get_SbbsKdf()->DeriveKey(sk, Key::ID(waddr->m_OwnID, Key::Type::Bbs));

                        proto::Sk2Pk(widMy.m_Pk, sk);

                        pc.Sign(sk);
                        params.SetParameter(TxParameterID::PaymentConfirmation, pc.m_Signature);
                    }
                }
            }
        }

        SendTxParameters(move(params));
    }

    void SimpleTransaction::NotifyTransactionRegistered()
    {
        TxParameters params;
		uint8_t nCode = proto::TxStatus::Ok; // compiler workaround (ref to static const)
        params.SetParameter(TxParameterID::TransactionRegistered, nCode);
        SendTxParameters(move(params));
    }

    bool SimpleTransaction::IsSelfTx() const
    {
        WalletID peerID = GetMandatoryParameter<WalletID>(TxParameterID::PeerID);
        auto address = m_WalletDB->getAddress(peerID);
        return address.is_initialized() && address->isOwn();
    }

    SimpleTransaction::State SimpleTransaction::GetState() const
    {
        State state = State::Initial;
        GetParameter(TxParameterID::State, state);
        return state;
    }

    bool SimpleTransaction::ShouldNotifyAboutChanges(TxParameterID paramID) const
    {
        switch (paramID)
        {
        case TxParameterID::Amount:
        case TxParameterID::Fee:
        case TxParameterID::MinHeight:
        case TxParameterID::PeerID:
        case TxParameterID::MyID:
        case TxParameterID::CreateTime:
        case TxParameterID::IsSender:
        case TxParameterID::Status:
        case TxParameterID::TransactionType:
        case TxParameterID::KernelID:
        case TxParameterID::AssetID:
            return true;
        default:
            return false;
        }
    }
}
