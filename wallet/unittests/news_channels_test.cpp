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

#include <boost/filesystem.hpp>

// test helpers and mocks
#include "test_helpers.h"
WALLET_TEST_INIT
#include "mock_bbs_network.cpp"

// tested module
#include "wallet/client/extensions/news_channels/interface.h"
#include "wallet/client/extensions/news_channels/updates_provider.h"
#include "wallet/client/extensions/news_channels/exchange_rate_provider.h"
#include "wallet/client/extensions/notifications/notification_center.h"

// dependencies
#include "wallet/client/extensions/broadcast_gateway/broadcast_router.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_validator.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_creator.h"

#include <tuple>

using namespace std;
using namespace beam;
using namespace beam::wallet;

namespace
{
    using PrivateKey = ECC::Scalar::Native;
    using PublicKey = PeerID;

    const string dbFileName = "wallet.db";

    /**
     *  Class to test correct notification of news channels observers
     */
    struct MockNewsObserver : public INewsObserver, public IExchangeRateObserver
    {
        using OnVersion = function<void(const VersionInfo&, const ECC::uintBig&)>;
        using OnRate = function<void(const std::vector<ExchangeRate>&)>;

        MockNewsObserver(OnVersion onVers, OnRate onRate)
            : m_onVers(onVers)
            , m_onRate(onRate) {};

        virtual void onNewWalletVersion(const VersionInfo& v, const ECC::uintBig& s) override
        {
            m_onVers(v, s);
        }
        virtual void onExchangeRates(const std::vector<ExchangeRate>& r) override
        {
            m_onRate(r);
        }

        OnVersion m_onVers;
        OnRate m_onRate;
    };

    /**
     *  Class to test notifications observers interface
     */
    struct MockNotificationsObserver : public INotificationsObserver
    {
        using OnNotification = function<void(ChangeAction action, const std::vector<Notification>&)>;

        MockNotificationsObserver(OnNotification callback)
            : m_onNotification(callback) {};
        
        virtual void onNotificationsChanged(ChangeAction action, const std::vector<Notification>& list) override
        {
            m_onNotification(action, list);
        }

        OnNotification m_onNotification;
    };

    IWalletDB::Ptr createSqliteWalletDB()
    {
        if (boost::filesystem::exists(dbFileName))
        {
            boost::filesystem::remove(dbFileName);
        }
        ECC::NoLeak<ECC::uintBig> seed;
        seed.V = 10283UL;
        auto walletDB = WalletDB::init(dbFileName, string("pass123"), seed);
        beam::Block::SystemState::ID id = { };
        id.m_Height = 134;
        walletDB->setSystemStateID(id);
        return walletDB;
    }

    /**
     *  Derive key pair with specified @keyIndex
     */
    std::tuple<PublicKey, PrivateKey> deriveKeypair(IWalletDB::Ptr storage, uint64_t keyIndex)
    {
        PrivateKey sk;
        PublicKey pk;
        storage->get_MasterKdf()->DeriveKey(sk, ECC::Key::ID(keyIndex, Key::Type::Bbs));
        pk.FromSk(sk);
        return std::make_tuple(pk, sk);
    }

    void TestSoftwareVersion()
    {
        cout << endl << "Test Version operations" << endl;

        {
            Version v {123, 456, 789};
            WALLET_CHECK(to_string(v) == "123.456.789");
        }

        {
            WALLET_CHECK(Version(12,12,12) == Version(12,12,12));
            WALLET_CHECK(!(Version(12,12,12) != Version(12,12,12)));
            WALLET_CHECK(Version(12,13,12) != Version(12,12,12));
            WALLET_CHECK(!(Version(12,13,12) == Version(12,12,12)));

            WALLET_CHECK(Version(12,12,12) < Version(13,12,12));
            WALLET_CHECK(Version(12,12,12) < Version(12,13,12));
            WALLET_CHECK(Version(12,12,12) < Version(12,12,13));
            WALLET_CHECK(Version(12,12,12) < Version(13,13,13));
            WALLET_CHECK(!(Version(12,12,12) < Version(12,12,12)));
        }

        {
            Version v;
            bool res = false;

            WALLET_CHECK_NO_THROW(res = v.from_string("12.345.6789"));
            WALLET_CHECK(res == true);
            WALLET_CHECK(v == Version(12,345,6789));

            WALLET_CHECK_NO_THROW(res = v.from_string("0.0.0"));
            WALLET_CHECK(res == true);
            WALLET_CHECK(v == Version());

            WALLET_CHECK_NO_THROW(res = v.from_string("12345.6789"));
            WALLET_CHECK(res == false);

            WALLET_CHECK_NO_THROW(res = v.from_string("12,345.6789"));
            WALLET_CHECK(res == false);

            WALLET_CHECK_NO_THROW(res = v.from_string("12.345.6e89"));
            WALLET_CHECK(res == false);

            WALLET_CHECK_NO_THROW(res = v.from_string("12345.6789.12.52"));
            WALLET_CHECK(res == false);

            WALLET_CHECK_NO_THROW(res = v.from_string("f12345.6789.52"));
            WALLET_CHECK(res == false);
        }

        {
            WALLET_CHECK("desktop" == VersionInfo::to_string(VersionInfo::Application::DesktopWallet));
            WALLET_CHECK(VersionInfo::Application::DesktopWallet == VersionInfo::from_string("desktop"));
        }
    }

    void TestNewsChannelsObservers()
    {
        cout << endl << "Test news channels observers" << endl;

        auto storage = createSqliteWalletDB();
        MockBbsNetwork network;
        BroadcastRouter broadcastRouter(network, network);
        BroadcastMsgValidator validator;
        AppUpdateInfoProvider updatesProvider(broadcastRouter, validator);
        ExchangeRateProvider rateProvider(broadcastRouter, validator, *storage);
        
        int execCountVers = 0;
        int execCountRate = 0;

        const VersionInfo verInfo { VersionInfo::Application::DesktopWallet, Version {123,456,789} };
        const std::vector<ExchangeRate> rates {
            { ExchangeRate::Currency::Beam, ExchangeRate::Currency::Usd, 147852369, getTimestamp() } };

        const auto& [pk, sk] = deriveKeypair(storage, 321);
        BroadcastMsg msgV = BroadcastMsgCreator::createSignedMessage(toByteBuffer(verInfo), sk);
        BroadcastMsg msgR = BroadcastMsgCreator::createSignedMessage(toByteBuffer(rates), sk);
        ECC::uintBig msgSignature;
        fromByteBuffer(msgV.m_signature, msgSignature);

        MockNewsObserver testObserver(
            [&execCountVers, &verInfo, &msgSignature]
            (const VersionInfo& v, const ECC::uintBig& id)
            {
                // check notification ID same as message signature
                WALLET_CHECK(msgSignature == id);
                WALLET_CHECK(verInfo == v);
                ++execCountVers;
            },
            [&execCountRate, &rates]
            (const std::vector<ExchangeRate>& r)
            {
                WALLET_CHECK(rates == r);
                ++execCountRate;
            });


        {
            // loading correct key with 2 additional just for filling
            PublicKey pk2, pk3;
            std::tie(pk2, std::ignore) = deriveKeypair(storage, 789);
            std::tie(pk3, std::ignore) = deriveKeypair(storage, 456);
            validator.setPublisherKeys({pk, pk2, pk3});
        }

        {
            cout << "Case: subscribed on valid message" << endl;
            updatesProvider.Subscribe(&testObserver);
            rateProvider.Subscribe(&testObserver);
            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msgV);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msgR);
            WALLET_CHECK(execCountVers == 1);
            WALLET_CHECK(execCountRate == 1);
        }
        {
            cout << "Case: unsubscribed on valid message" << endl;
            updatesProvider.Unsubscribe(&testObserver);
            rateProvider.Unsubscribe(&testObserver);
            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msgV);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msgR);
            WALLET_CHECK(execCountVers == 1);
            WALLET_CHECK(execCountRate == 1);
        }
        {
            cout << "Case: subscribed back" << endl;
            updatesProvider.Subscribe(&testObserver);
            rateProvider.Subscribe(&testObserver);
            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msgV);
            WALLET_CHECK(execCountVers == 2);
            WALLET_CHECK(execCountRate == 1);   // the rate was the same so no need in the notification
        }
        {
            cout << "Case: subscribed on invalid message" << endl;
            // sign the same message with other key
            PrivateKey newSk;
            std::tie(std::ignore, newSk) = deriveKeypair(storage, 322);
            msgV = BroadcastMsgCreator::createSignedMessage(toByteBuffer(verInfo), newSk);
            broadcastRouter.sendMessage(BroadcastContentType::SoftwareUpdates, msgV);
            WALLET_CHECK(execCountVers == 2);
        }
        cout << "Test end" << endl;
    }

    void TestExchangeRateProvider()
    {
        cout << endl << "Test ExchangeRateProvider" << endl;

        MockBbsNetwork network;
        BroadcastRouter broadcastRouter(network, network);
        BroadcastMsgValidator validator;
        auto storage = createSqliteWalletDB();
        ExchangeRateProvider rateProvider(broadcastRouter, validator, *storage);

        const auto& [pk, sk] = deriveKeypair(storage, 321);
        validator.setPublisherKeys({pk});

        // empty provider
        {
            cout << "Case: empty rates" << endl;
            WALLET_CHECK(rateProvider.getRates().empty());
        }
        const std::vector<ExchangeRate> rate {
            { ExchangeRate::Currency::Beam, ExchangeRate::Currency::Usd, 147852369, getTimestamp() }
        };
        // add rate
        {
            cout << "Case: add rates" << endl;
            BroadcastMsg msg = BroadcastMsgCreator::createSignedMessage(toByteBuffer(rate), sk);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msg);

            auto testRates = rateProvider.getRates();
            WALLET_CHECK(testRates.size() == 1);
            WALLET_CHECK(testRates[0] == rate[0]);
        }
        // update rate
        {
            cout << "Case: not update if rates older" << endl;
            const std::vector<ExchangeRate> rateOlder {
                { ExchangeRate::Currency::Beam, ExchangeRate::Currency::Usd, 14785238554, getTimestamp()-100 }
            };
            BroadcastMsg msg = BroadcastMsgCreator::createSignedMessage(toByteBuffer(rateOlder), sk);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msg);

            auto testRates = rateProvider.getRates();
            WALLET_CHECK(testRates.size() == 1);
            WALLET_CHECK(testRates[0] == rate[0]);
        }
        const std::vector<ExchangeRate> rateNewer {
            { ExchangeRate::Currency::Beam, ExchangeRate::Currency::Usd, 14785238554, getTimestamp()+100 }
        };
        {
            cout << "Case: update rates" << endl;
            BroadcastMsg msg = BroadcastMsgCreator::createSignedMessage(toByteBuffer(rateNewer), sk);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msg);

            auto testRates = rateProvider.getRates();
            WALLET_CHECK(testRates.size() == 1);
            WALLET_CHECK(testRates[0] == rateNewer[0]);
        }
        // add more rate
        {
            cout << "Case: add more rates" << endl;
            const std::vector<ExchangeRate> rateAdded {
                { ExchangeRate::Currency::Beam, ExchangeRate::Currency::Bitcoin, 987, getTimestamp()+100 }
            };
            BroadcastMsg msg = BroadcastMsgCreator::createSignedMessage(toByteBuffer(rateAdded), sk);
            broadcastRouter.sendMessage(BroadcastContentType::ExchangeRates, msg);

            auto testRates = rateProvider.getRates();
            WALLET_CHECK(testRates.size() == 2);
            WALLET_CHECK(testRates[0] == rateNewer[0] || testRates[1] == rateNewer[0]);
        }
    }

    void TestNotificationCenter()
    {
        cout << endl << "Test NotificationCenter" << endl;

        auto storage = createSqliteWalletDB();
        std::map<Notification::Type,bool> activeTypes {
            { Notification::Type::SoftwareUpdateAvailable, true },
            { Notification::Type::AddressStatusChanged, true },
            { Notification::Type::TransactionStatusChanged, true },
            { Notification::Type::BeamNews, true }
        };
        NotificationCenter center(*storage, activeTypes);

        {
            {
                cout << "Case: empty list" << endl;
                WALLET_CHECK(center.getNotifications().size() == 0);
            }

            const ECC::uintBig id({0,1,2,3,4,5,6,7,8,9,
                              0,1,2,3,4,5,6,7,8,9,
                              0,1,2,3,4,5,6,7,8,9,
                              0,1});
            const VersionInfo info { VersionInfo::Application::DesktopWallet, Version(1,2,3) };
            
            {
                cout << "Case: create notification" << endl;
                size_t execCount = 0;
                MockNotificationsObserver observer(
                    [&execCount, &id]
                    (ChangeAction action, const std::vector<Notification>& list)
                    {
                        WALLET_CHECK(action == ChangeAction::Added);
                        WALLET_CHECK(list.size() == 1);
                        WALLET_CHECK(list[0].m_ID == id);
                        WALLET_CHECK(list[0].m_state == Notification::State::Unread);
                        ++execCount;
                    }
                );
                center.Subscribe(&observer);
                center.onNewWalletVersion(info, id);
                auto list = center.getNotifications();
                WALLET_CHECK(list.size() == 1);
                WALLET_CHECK(list[0].m_ID == id);
                WALLET_CHECK(list[0].m_type == Notification::Type::SoftwareUpdateAvailable);
                WALLET_CHECK(list[0].m_state == Notification::State::Unread);
                WALLET_CHECK(list[0].m_createTime != 0);
                WALLET_CHECK(list[0].m_content == toByteBuffer(info));
                WALLET_CHECK(execCount == 1);
                center.Unsubscribe(&observer);
            }

            // update: mark as read
            {
                cout << "Case: mark as read" << endl;
                size_t execCount = 0;
                MockNotificationsObserver observer(
                    [&execCount, &id]
                    (ChangeAction action, const std::vector<Notification>& list)
                    {
                        WALLET_CHECK(action == ChangeAction::Updated);
                        WALLET_CHECK(list.size() == 1);
                        WALLET_CHECK(list[0].m_ID == id);
                        WALLET_CHECK(list[0].m_state == Notification::State::Read);
                        ++execCount;
                    }
                );
                center.Subscribe(&observer);
                center.markNotificationAsRead(id);
                auto list = center.getNotifications();
                WALLET_CHECK(list.size() == 1);
                WALLET_CHECK(list[0].m_ID == id);
                WALLET_CHECK(list[0].m_type == Notification::Type::SoftwareUpdateAvailable);
                WALLET_CHECK(list[0].m_state == Notification::State::Read);
                WALLET_CHECK(list[0].m_createTime != 0);
                WALLET_CHECK(list[0].m_content == toByteBuffer(info));
                WALLET_CHECK(execCount == 1);
                center.Unsubscribe(&observer);
            }

            // delete
            {
                cout << "Case: delete notification" << endl;
                size_t execCount = 0;
                MockNotificationsObserver observer(
                    [&execCount, &id]
                    (ChangeAction action, const std::vector<Notification>& list)
                    {
                        WALLET_CHECK(action == ChangeAction::Removed);
                        WALLET_CHECK(list.size() == 1);
                        WALLET_CHECK(list[0].m_ID == id);
                        ++execCount;
                    }
                );
                center.Subscribe(&observer);
                center.deleteNotification(id);
                auto list = center.getNotifications();
                WALLET_CHECK(list.size() == 0);
                WALLET_CHECK(execCount == 1);
                center.Unsubscribe(&observer);
            }

            // check on duplicate
            {
                cout << "Case: duplicate notification" << endl;
                MockNotificationsObserver observer(
                    []
                    (ChangeAction action, const std::vector<Notification>& list)
                    {
                        WALLET_CHECK(false);
                    }
                );
                center.Subscribe(&observer);
                center.onNewWalletVersion(info, id);
                auto list = center.getNotifications();
                WALLET_CHECK(list.size() == 0);
                center.Unsubscribe(&observer);
            }
        }
    }

    void TestNotificationsOnOffSwitching()
    {
        cout << endl << "Test notifications on/off switching" << endl;

        auto storage = createSqliteWalletDB();
        std::map<Notification::Type,bool> activeTypes {
            { Notification::Type::SoftwareUpdateAvailable, false },
            { Notification::Type::AddressStatusChanged, false },
            { Notification::Type::TransactionStatusChanged, false },
            { Notification::Type::BeamNews, false }
        };
        NotificationCenter center(*storage, activeTypes);

        WALLET_CHECK(center.getNotifications().size() == 0);

        VersionInfo info { VersionInfo::Application::DesktopWallet, Version(1,2,3) };
        const ECC::uintBig id( {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
                                1,1,1,1,1,1,1,1,1,1,1,1});
        const ECC::uintBig id2({2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
                                2,2,2,2,2,2,2,2,2,2,2,2});
        const ECC::uintBig id3({3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
                                3,3,3,3,3,3,3,3,3,3,3,3});

        // notifications is off on start
        {
            cout << "Case: notifications is off on start" << endl;
            MockNotificationsObserver observer(
                []
                (ChangeAction action, const std::vector<Notification>& list)
                {
                    WALLET_CHECK(false);
                }
            );
            center.Subscribe(&observer);
            center.onNewWalletVersion(info, id);
            auto list = center.getNotifications();
            WALLET_CHECK(list.size() == 0);
            center.Unsubscribe(&observer);
        }
        // notifications switched on
        {
            cout << "Case: notifications switched on" << endl;
            size_t execCount = 0;
            MockNotificationsObserver observer(
                [&execCount, &id2]
                (ChangeAction action, const std::vector<Notification>& list)
                {
                    WALLET_CHECK(action == ChangeAction::Added);
                    WALLET_CHECK(list.size() == 1);
                    WALLET_CHECK(list[0].m_ID == id2);
                    ++execCount;
                }
            );
            center.switchOnOffNotifications(Notification::Type::SoftwareUpdateAvailable, true);
            center.Subscribe(&observer);
            auto list = center.getNotifications();
            WALLET_CHECK(list.size() == 1);
            center.onNewWalletVersion(info, id2);
            list = center.getNotifications();
            WALLET_CHECK(list.size() == 2);
            center.Unsubscribe(&observer);
            WALLET_CHECK(execCount == 1);
        }
        // notification switched off
        {
            cout << "Case: notifications switched on" << endl;
            MockNotificationsObserver observer(
                []
                (ChangeAction action, const std::vector<Notification>& list)
                {
                    WALLET_CHECK(false);
                }
            );
            center.switchOnOffNotifications(Notification::Type::SoftwareUpdateAvailable, false);
            center.Subscribe(&observer);
            center.onNewWalletVersion(info, id3);
            auto list = center.getNotifications();
            WALLET_CHECK(list.size() == 0);
            center.Unsubscribe(&observer);
        }
    }

} // namespace

int main()
{
    cout << "News channels tests:" << endl;

    io::Reactor::Ptr mainReactor{ io::Reactor::create() };
    io::Reactor::Scope scope(*mainReactor);

    TestSoftwareVersion();

    TestNewsChannelsObservers();

    TestExchangeRateProvider();

    TestNotificationCenter();
    TestNotificationsOnOffSwitching();
    
    boost::filesystem::remove(dbFileName);

    assert(g_failureCount == 0);
    return WALLET_CHECK_RESULT;
}

