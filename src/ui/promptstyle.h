#pragma once
#include <QtWidgets>
namespace rs {
// Shared only by message/conflict prompts, never ordinary toolbar controls.
void promptDocumentSpacing(QTextDocument *document);
void stylePromptText(QLabel *label);
void stylePromptText(QTextEdit *text);
void stylePromptButton(QPushButton *button);
}
