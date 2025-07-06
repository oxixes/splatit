#ifndef SPLATOON_SERVER_PROMISE_HPP
#define SPLATOON_SERVER_PROMISE_HPP

#include <functional>
#include <any>

struct MoveOnlyCallable {
    virtual void operator()(std::any&&) = 0;
    virtual ~MoveOnlyCallable() = default;
};

template<typename F>
struct MoveOnlyCallableImpl : MoveOnlyCallable {
    F func;
    explicit MoveOnlyCallableImpl(F&& f) : func(std::move(f)) {}
    void operator()(std::any&& val) override { func(std::move(val)); }
};

class Promise {
public:
    using Callback = std::unique_ptr<MoveOnlyCallable>;

    Promise() = default;

    template<typename F>
    void then(F&& then) {
        callback = std::make_unique<MoveOnlyCallableImpl<F>>(std::forward<F>(then));
        if (resolved && callback) {
            (*callback)(std::move(value));
        }
    }

    void setResolveValue(std::any val) {
        this->value = std::move(val);
    }

    void resolve() {
        if (callback) {
            (*callback)(std::move(value));
        }

        resolved = true;
    }

    template<typename U>
    Promise& setContext(U&& ctx) {
        context = std::forward<U>(ctx);
        return *this;
    }

    template<typename U>
    [[nodiscard]] U getContext() const {
        return std::move(std::any_cast<U>(context));
    }

    [[nodiscard]] bool hasContext() const {
        return context.has_value();
    }

private:
    Callback callback;
    std::any value;

    bool resolved = false;

    std::any context;
};

class PromiseAll : public std::enable_shared_from_this<PromiseAll> {
public:
    using Callback = std::unique_ptr<MoveOnlyCallable>;

    explicit PromiseAll(std::vector<std::shared_ptr<Promise>> promises)
            : promises(std::move(promises)), resolvedCount(0), results(this->promises.size()) {
        auto self = this->shared_from_this();
        for (size_t i = 0; i < promises.size(); ++i) {
            if (context.has_value()) {
                promises[i]->setContext(context);
            }

            promises[i]->then([self, i](std::any&& val) {
                self->results[i] = std::move(val);
                if (++self->resolvedCount == self->promises.size()) {
                    self->resolved = true;
                    if (self->callback) {
                        (*self->callback)(std::move(self->results));
                    }
                }
            });
        }
    }

    template<typename F>
    void then(F&& then) {
        callback = std::make_unique<MoveOnlyCallableImpl<F>>(std::forward<F>(then));
        if (resolved && callback) {
            (*callback)(std::move(results));
        }
    }

    template<typename U>
    PromiseAll& setContext(U&& ctx) {
        context = std::forward<U>(ctx);
        return *this;
    }

    template<typename U>
    [[nodiscard]] U getContext() const {
        return std::move(std::any_cast<U>(context));
    }

private:
    std::vector<std::shared_ptr<Promise>> promises;
    std::vector<std::any> results;
    size_t resolvedCount;
    Callback callback;

    bool resolved = false;

    std::any context;
};

#endif //SPLATOON_SERVER_PROMISE_HPP
