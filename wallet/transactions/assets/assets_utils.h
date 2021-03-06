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
#pragma once

#include <core/block_crypt.h>
#include "common.h"

namespace beam::wallet {
    class AssetMeta
    {
    public:
        explicit AssetMeta(std::string meta);
        explicit AssetMeta(const Asset::Full& info);

        bool isStd() const;
        void LogInfo(const std::string& prefix = "") const;

    private:
        void Parse();

        typedef std::map<std::string, std::string> SMap;
        SMap _values;
        bool _std;
        std::string _meta;
    };
}
