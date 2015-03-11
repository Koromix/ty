/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "descriptor_set_notifier.hh"

using namespace std;

DescriptorSetNotifier::DescriptorSetNotifier(ty_descriptor_set *set, QObject *parent)
    : QObject(parent)
{
    if (set)
        addDescriptorSet(set);

    interval_timer_.setSingleShot(true);
    connect(&interval_timer_, &QTimer::timeout, this, &DescriptorSetNotifier::restoreNotifiers);
}

void DescriptorSetNotifier::setDescriptorSet(ty_descriptor_set *set)
{
    clear();
    addDescriptorSet(set);
}

void DescriptorSetNotifier::addDescriptorSet(ty_descriptor_set *set)
{
    for (unsigned int i = 0; i < set->count; i++) {
#ifdef _WIN32
        auto notifier = make_unique<QWinEventNotifier>(set->desc[i]);
        connect(notifier.get(), &QWinEventNotifier::activated, this, &DescriptorSetNotifier::activatedDesc);
#else
        auto notifier = make_unique<QSocketNotifier>(set->desc[i], QSocketNotifier::Read);
        connect(notifier.get(), &QSocketNotifier::activated, this, &DescriptorSetNotifier::activatedDesc);
#endif

        notifier->setEnabled(enabled_);

        notifiers_.push_back(move(notifier));
    }
}

void DescriptorSetNotifier::setMinInterval(int interval)
{
    if (interval < 0)
        interval = 0;

    interval_timer_.setInterval(interval);
}

bool DescriptorSetNotifier::isEnabled() const
{
    return enabled_;
}

int DescriptorSetNotifier::minInterval() const
{
    return interval_timer_.interval();
}

void DescriptorSetNotifier::setEnabled(bool enable)
{
    enabled_ = enable;

    for (auto &notifier: notifiers_)
        notifier->setEnabled(enable);
}

void DescriptorSetNotifier::clear()
{
    notifiers_.clear();
}

void DescriptorSetNotifier::activatedDesc(ty_descriptor desc)
{
    if (!enabled_ )
        return;

    if (interval_timer_.interval() > 0) {
        setEnabled(false);
        enabled_ = true;

        interval_timer_.start();
    }

    emit activated(desc);
}

void DescriptorSetNotifier::restoreNotifiers()
{
    if (!enabled_)
        return;

    setEnabled(true);
}
