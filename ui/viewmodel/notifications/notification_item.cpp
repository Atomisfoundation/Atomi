// Copyright 2020 The Beam Team
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

#include "notification_item.h"
#include "utility/helpers.h"
#include "wallet/core/common.h"
#include "ui/viewmodel/ui_helpers.h"
#include "ui/viewmodel/qml_globals.h"

using namespace beam::wallet;

NotificationItem::NotificationItem(const Notification& notification)
    : m_notification{notification}
{}

bool NotificationItem::operator==(const NotificationItem& other) const
{
    return getID() == other.getID();
}

ECC::uintBig NotificationItem::getID() const
{
    return m_notification.m_ID;
}

QDateTime NotificationItem::timeCreated() const
{
    QDateTime datetime;
    datetime.setTime_t(m_notification.m_createTime);
    return datetime;
}

QString NotificationItem::title() const
{
    switch(m_notification.m_type)
    {
        case Notification::Type::SoftwareUpdateAvailable:
        {
            VersionInfo info;
            if (fromByteBuffer(m_notification.m_content, info))
            {
                QString ver = QString::fromStdString(info.m_version.to_string());
                QString title("New version v");
                title.append(ver);
                title.append(" is avalable");
                return title;
            }
            else
            {
                LOG_ERROR() << "Software update notification deserialization error";
                return QString();
            }
        }
        case Notification::Type::AddressStatusChanged:
            return "Address expired";
        case Notification::Type::TransactionStatusChanged:
            return "Transaction received";
        case Notification::Type::BeamNews:
            return "BEAM in the press";
        default:
            return "error";
    }
}

QString NotificationItem::message() const
{
    switch(m_notification.m_type)
    {
        case Notification::Type::SoftwareUpdateAvailable:
        {
            VersionInfo info;
            if (fromByteBuffer(m_notification.m_content, info))
            {
                QString currentVer = QString::fromStdString(beamui::getCurrentAppVersion().to_string());
                QString message("Your current version is v");
                message.append(currentVer);
                message.append(". Please update to get the most of your Beam wallet.");
                return message;
            }
            else
            {
                LOG_ERROR() << "Software update notification deserialization error";
                return QString();
            }
        }
        case Notification::Type::AddressStatusChanged:
            return "Address expired";
        case Notification::Type::TransactionStatusChanged:
            return "Transaction received";
        case Notification::Type::BeamNews:
            return "BEAM in the press";
        default:
            return "error";
    }
}

QString NotificationItem::type() const
{
    // !TODO: full list of the supported item types is: update expired received sent failed inpress hotnews videos events newsletter community
    
    switch(m_notification.m_type)
    {
        case Notification::Type::SoftwareUpdateAvailable:
            return "update";
        case Notification::Type::AddressStatusChanged:
            return "expired";
        case Notification::Type::TransactionStatusChanged:
            return "received"; // or "sent" or "failed"
        case Notification::Type::BeamNews:
            return "newsletter";
        default:
            return "error";
    }
}

QString NotificationItem::state() const
{
    switch(m_notification.m_state)
    {
        case Notification::State::Unread:
            return "unread";
        case Notification::State::Read:
            return "read";
        case Notification::State::Deleted:
            return "deleted";
        default:
            return "error";
    }
}
