#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

struct JSContext;
struct JSRuntime;

namespace PluginSystem {

class ScriptEngine final {
public:
	ScriptEngine() = default;
	~ScriptEngine();

	ScriptEngine(const ScriptEngine &) = delete;
	ScriptEngine &operator=(const ScriptEngine &) = delete;

	[[nodiscard]] bool create(size_t memoryLimitBytes);
	void destroy();

	[[nodiscard]] bool valid() const;
	[[nodiscard]] JSContext *context() const;
	[[nodiscard]] JSRuntime *runtime() const;

	void setOpaque(void *opaque);
	[[nodiscard]] void *opaque() const;

	[[nodiscard]] bool eval(
		const QByteArray &source,
		const QString &filename,
		QString *error);
	[[nodiscard]] bool callGlobalFunction(
		const char *name,
		QString *error);
	[[nodiscard]] bool callStoredFunction(
		const QString &slot,
		QString *error);

	void storeFunction(const QString &slot, void *jsValuePtr);
	void clearStoredFunctions();

private:
	JSRuntime *_runtime = nullptr;
	JSContext *_context = nullptr;
};

} // namespace PluginSystem
