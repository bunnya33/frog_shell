#pragma once
#include <QtCore>
#include <functional>
namespace rs::services {
struct Command { QByteArray text, input; int timeoutMs = 15000, outputLimit = 2 * 1024 * 1024; bool mutation = false; };
struct Result { QByteArray out, err; int code = -1; bool unknown = false; };
using Runner = std::function<Result(const Command &)>;
bool validUnit(const QString &unit);
bool isTemplate(const QString &unit);
QString templateRestriction();
QStringList arguments(const QString &text); // Quotes group arguments; no shell evaluation.
QByteArray configuration(const QVariantMap &form);
QString stateText(const QVariantMap &unit);
QString bootText(const QString &state);
QVariantMap properties(const QByteArray &text);
QString deletionRestriction(const QVariantMap &unit);
// A job is executed serially by one command-only Session, including all verification steps.
QVariantMap run(const QString &action, const QVariantMap &request, const Runner &runner);
}
