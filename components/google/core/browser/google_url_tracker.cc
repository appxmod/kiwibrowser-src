// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/google/core/browser/google_url_tracker.h"

#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/data_use_measurement/core/data_use_user_data.h"
#include "components/google/core/browser/google_pref_names.h"
#include "components/google/core/browser/google_switches.h"
#include "components/google/core/browser/google_util.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"

const char GoogleURLTracker::kDefaultGoogleHomepage[] =
    "https://www.google.com/";
const char GoogleURLTracker::kSearchDomainCheckURL[] =
    "https://www.google.com/searchdomaincheck?format=domain&type=chrome";
const base::Feature GoogleURLTracker::kNoSearchDomainCheck{
    "NoSearchDomainCheck", base::FEATURE_DISABLED_BY_DEFAULT};

GoogleURLTracker::GoogleURLTracker(
    std::unique_ptr<GoogleURLTrackerClient> client,
    Mode mode)
    : client_(std::move(client)),
      google_url_(
          mode == ALWAYS_DOT_COM_MODE
              ? kDefaultGoogleHomepage
              : client_->GetPrefs()->GetString(prefs::kLastKnownGoogleURL)),
      in_startup_sleep_(true),
      already_loaded_(false),
      need_to_load_(false),
      weak_ptr_factory_(this) {
  net::NetworkChangeNotifier::AddNetworkChangeObserver(this);
  client_->set_google_url_tracker(this);

  // Because this function can be called during startup, when kicking off a URL
  // load can eat up 20 ms of time, we delay five seconds, which is hopefully
  // long enough to be after startup, but still get results back quickly.
  // Ideally, instead of this timer, we'd do something like "check if the
  // browser is starting up, and if so, come back later", but there is currently
  // no function to do this.
  //
  // In ALWAYS_DOT_COM_MODE we do not nothing at all (but in unit tests
  // /searchdomaincheck lookups might still be issued by calling FinishSleep
  // manually).
  if (mode == NORMAL_MODE) {
    static const int kStartLoadDelayMS = 5000;
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&GoogleURLTracker::FinishSleep,
                       weak_ptr_factory_.GetWeakPtr()),
        base::TimeDelta::FromMilliseconds(kStartLoadDelayMS));
  }
}

GoogleURLTracker::~GoogleURLTracker() {
}

// static
void GoogleURLTracker::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(prefs::kLastKnownGoogleURL,
                               GoogleURLTracker::kDefaultGoogleHomepage);
  registry->RegisterStringPref(prefs::kLastPromptedGoogleURL, std::string());
}

void GoogleURLTracker::RequestServerCheck() {
  if (!simple_loader_)
    SetNeedToLoad();
}

std::unique_ptr<GoogleURLTracker::Subscription>
GoogleURLTracker::RegisterCallback(const OnGoogleURLUpdatedCallback& cb) {
  return callback_list_.Add(cb);
}

void GoogleURLTracker::OnURLLoaderComplete(
    std::unique_ptr<std::string> response_body) {
  // Delete the loader.
  simple_loader_.reset();

  // Don't update the URL if the request didn't succeed.
  if (!response_body) {
    already_loaded_ = false;
    return;
  }

  // See if the response data was valid. It should be ".google.<TLD>".
  base::TrimWhitespaceASCII(*response_body, base::TRIM_ALL,
                            response_body.get());
  if (!base::StartsWith(*response_body, ".google.",
                        base::CompareCase::INSENSITIVE_ASCII))
    return;
  GURL url("https://www" + *response_body);
  if (!url.is_valid() || (url.path().length() > 1) || url.has_query() ||
      url.has_ref() ||
      !google_util::IsGoogleDomainUrl(url, google_util::DISALLOW_SUBDOMAIN,
                                      google_util::DISALLOW_NON_STANDARD_PORTS))
    return;

  if (url != google_url_) {
    google_url_ = url;
    client_->GetPrefs()->SetString(prefs::kLastKnownGoogleURL,
                                   google_url_.spec());
    callback_list_.Notify();
  }
}

void GoogleURLTracker::OnNetworkChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  // Ignore destructive signals.
  if (type == net::NetworkChangeNotifier::CONNECTION_NONE)
    return;
  already_loaded_ = false;
  StartLoadIfDesirable();
}

void GoogleURLTracker::Shutdown() {
  client_.reset();
  simple_loader_.reset();
  weak_ptr_factory_.InvalidateWeakPtrs();
  net::NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
}

void GoogleURLTracker::SetNeedToLoad() {
  need_to_load_ = true;
  StartLoadIfDesirable();
}

void GoogleURLTracker::FinishSleep() {
  in_startup_sleep_ = false;
  StartLoadIfDesirable();
}

void GoogleURLTracker::StartLoadIfDesirable() {
  //屏蔽搜索引擎列表的加载
}
