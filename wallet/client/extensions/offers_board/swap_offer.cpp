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

#include "wallet/client/extensions/offers_board/swap_offer.h"

namespace beam::wallet
{

SwapOffer::SwapOffer(const boost::optional<TxID>& txID) : TxParameters(txID) {}

SwapOffer::SwapOffer(const TxID& txId,
                     SwapOfferStatus status,
                     WalletID publisherId,
                     AtomicSwapCoin coin)
    : TxParameters(txId),
      m_txId(txId),
      m_status(status),
      m_publisherId(publisherId),
      m_coin(coin) {}

SwapOffer::SwapOffer(const TxParameters& params) 
    : TxParameters(params)
 {
    auto id = GetTxID();
    if (id)
        m_txId = *id;
    TxStatus txStatus;
    if (GetParameter(TxParameterID::Status, txStatus))
    {
        switch(txStatus)
        {
        case TxStatus::Pending :
            m_status = SwapOfferStatus::Pending;
            break;
        case TxStatus::InProgress :
            m_status = SwapOfferStatus::InProgress;
            break;
        case TxStatus::Canceled :
            m_status = SwapOfferStatus::Canceled;
            break;
        case TxStatus::Completed :
            m_status = SwapOfferStatus::Completed;
            break;
        case TxStatus::Failed :
            TxFailureReason failureReason;
            if (GetParameter(TxParameterID::InternalFailureReason,
                             failureReason) &&
                failureReason == TxFailureReason::TransactionExpired)
            {
                m_status = SwapOfferStatus::Expired;
            }
            else
            {
                m_status = SwapOfferStatus::Failed;
            }
            break;
        case TxStatus::Registering :
            m_status = SwapOfferStatus::InProgress;
            break;
        default:
            m_status = SwapOfferStatus::Pending;
        }
    }

    AtomicSwapCoin coin;
    if (GetParameter(TxParameterID::AtomicSwapCoin, coin))
    {
        m_coin = coin;
    }
}

void SwapOffer::SetTxParameters(const PackedTxParameters& parameters)
{
    // Do not forget to set other SwapOffer members also!
    SubTxID subTxID = kDefaultSubTxID;
    Deserializer d;
    for (const auto& p : parameters)
    {
        if (p.first == TxParameterID::SubTxIndex)
        {
            // change subTxID
            d.reset(p.second.data(), p.second.size());
            d & subTxID;
            continue;
        }

        SetParameter(p.first, p.second, subTxID);
    }
}

bool SwapOffer::isBeamSide() const
{
    bool res;
    GetParameter(TxParameterID::AtomicSwapIsBeamSide, res);
    return res;
}

Amount SwapOffer::amountBeam() const
{
    Amount amount;
    if (GetParameter(TxParameterID::Amount, amount))
    {
        return amount;
    }
    return 0;
}

Amount SwapOffer::amountSwapCoin() const
{
    Amount amount;
    if (GetParameter(TxParameterID::AtomicSwapAmount, amount))
    {
        return amount;
    }
    return 0;
}

AtomicSwapCoin SwapOffer::swapCoinType() const
{
    if (m_coin == AtomicSwapCoin::Unknown)
    {
        GetParameter(TxParameterID::AtomicSwapCoin, m_coin);
    }
    return m_coin;
}

Timestamp SwapOffer::timeCreated() const
{
    Timestamp time;
    GetParameter(TxParameterID::CreateTime, time);
    return time;
}

Height SwapOffer::peerResponseHeight() const
{
    Height peerResponseHeight;
    GetParameter(beam::wallet::TxParameterID::PeerResponseTime,
                 peerResponseHeight);
    return peerResponseHeight;
}

Height SwapOffer::minHeight() const
{
    Height minHeight;
    GetParameter(beam::wallet::TxParameterID::MinHeight, minHeight);
    return minHeight;
}

} // namespace beam::wallet
