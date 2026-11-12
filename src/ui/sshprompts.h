#pragma once
#include "../core/profile.h"
#include <QtWidgets>
#include <functional>
namespace rs {
// One modeless trust prompt at a time; simultaneous SSH consumers share the decision.
void requestHostTrust(QWidget *owner, const Profile &profile, const QString &fingerprint, bool changed,
                      std::function<bool()> valid, std::function<void(bool)> done);
}
