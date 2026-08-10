#include "plugin_system/plugins_script_engine.h"

extern "C" {
#include <quickjs.h>
}

#include <QtCore/QHash>

namespace PluginSystem {
namespace {

[[nodiscard]] QString ExceptionToString(JSContext *ctx) {
	const auto exception = JS_GetException(ctx);
	const auto cstr = JS_ToCString(ctx, exception);
	auto text = cstr
		? QString::fromUtf8(cstr)
		: QStringLiteral("JavaScript error");
	if (cstr) {
		JS_FreeCString(ctx, cstr);
	}
	JS_FreeValue(ctx, exception);
	return text;
}

struct StoredFns {
	QHash<QString, JSValue> values;
};

StoredFns *GetStore(JSContext *ctx) {
	return static_cast<StoredFns*>(JS_GetRuntimeOpaque(JS_GetRuntime(ctx)));
}

} // namespace

ScriptEngine::~ScriptEngine() {
	destroy();
}

bool ScriptEngine::create(size_t memoryLimitBytes) {
	destroy();
	_runtime = JS_NewRuntime();
	if (!_runtime) {
		return false;
	}
	JS_SetMemoryLimit(_runtime, memoryLimitBytes);
	JS_SetMaxStackSize(_runtime, 256 * 1024);
	auto *store = new StoredFns();
	JS_SetRuntimeOpaque(_runtime, store);
	_context = JS_NewContext(_runtime);
	if (!_context) {
		destroy();
		return false;
	}
	return true;
}

void ScriptEngine::destroy() {
	if (_context) {
		clearStoredFunctions();
		JS_FreeContext(_context);
		_context = nullptr;
	}
	if (_runtime) {
		if (auto *store = static_cast<StoredFns*>(
				JS_GetRuntimeOpaque(_runtime))) {
			delete store;
			JS_SetRuntimeOpaque(_runtime, nullptr);
		}
		JS_FreeRuntime(_runtime);
		_runtime = nullptr;
	}
}

bool ScriptEngine::valid() const {
	return _runtime && _context;
}

JSContext *ScriptEngine::context() const {
	return _context;
}

JSRuntime *ScriptEngine::runtime() const {
	return _runtime;
}

void ScriptEngine::setOpaque(void *opaque) {
	if (_context) {
		JS_SetContextOpaque(_context, opaque);
	}
}

void *ScriptEngine::opaque() const {
	return _context ? JS_GetContextOpaque(_context) : nullptr;
}

bool ScriptEngine::eval(
		const QByteArray &source,
		const QString &filename,
		QString *error) {
	if (!valid()) {
		if (error) {
			*error = QStringLiteral("Script engine is not ready.");
		}
		return false;
	}
	const auto file = filename.toUtf8();
	const auto result = JS_Eval(
		_context,
		source.constData(),
		source.size(),
		file.constData(),
		JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(result)) {
		if (error) {
			*error = ExceptionToString(_context);
		}
		JS_FreeValue(_context, result);
		return false;
	}
	JS_FreeValue(_context, result);
	return true;
}

bool ScriptEngine::callGlobalFunction(const char *name, QString *error) {
	if (!valid()) {
		if (error) {
			*error = QStringLiteral("Script engine is not ready.");
		}
		return false;
	}
	const auto global = JS_GetGlobalObject(_context);
	const auto fn = JS_GetPropertyStr(_context, global, name);
	JS_FreeValue(_context, global);
	if (!JS_IsFunction(_context, fn)) {
		JS_FreeValue(_context, fn);
		return true; // optional hook
	}
	const auto result = JS_Call(
		_context,
		fn,
		JS_UNDEFINED,
		0,
		nullptr);
	JS_FreeValue(_context, fn);
	if (JS_IsException(result)) {
		if (error) {
			*error = ExceptionToString(_context);
		}
		JS_FreeValue(_context, result);
		return false;
	}
	JS_FreeValue(_context, result);
	return true;
}

void ScriptEngine::storeFunction(const QString &slot, void *jsValuePtr) {
	if (!valid() || !jsValuePtr) {
		return;
	}
	auto *store = GetStore(_context);
	if (!store) {
		return;
	}
	auto value = *static_cast<JSValue*>(jsValuePtr);
	if (store->values.contains(slot)) {
		JS_FreeValue(_context, store->values[slot]);
	}
	store->values.insert(slot, JS_DupValue(_context, value));
}

void ScriptEngine::clearStoredFunctions() {
	if (!_context || !_runtime) {
		return;
	}
	auto *store = static_cast<StoredFns*>(JS_GetRuntimeOpaque(_runtime));
	if (!store) {
		return;
	}
	for (auto i = store->values.begin(); i != store->values.end(); ++i) {
		JS_FreeValue(_context, i.value());
	}
	store->values.clear();
}

bool ScriptEngine::callStoredFunction(const QString &slot, QString *error) {
	if (!valid()) {
		if (error) {
			*error = QStringLiteral("Script engine is not ready.");
		}
		return false;
	}
	auto *store = GetStore(_context);
	if (!store || !store->values.contains(slot)) {
		if (error) {
			*error = QStringLiteral("Action handler is not registered.");
		}
		return false;
	}
	const auto fn = store->values.value(slot);
	const auto result = JS_Call(
		_context,
		fn,
		JS_UNDEFINED,
		0,
		nullptr);
	if (JS_IsException(result)) {
		if (error) {
			*error = ExceptionToString(_context);
		}
		JS_FreeValue(_context, result);
		return false;
	}
	JS_FreeValue(_context, result);
	return true;
}

} // namespace PluginSystem
