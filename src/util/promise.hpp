#ifndef SPLATOON_SERVER_PROMISE_HPP
#define SPLATOON_SERVER_PROMISE_HPP

#include <functional>
#include <any>

template<typename T>
struct MoveOnlyCallable {
    virtual void operator()(T) = 0;
    virtual ~MoveOnlyCallable() = default;
};

template<typename F, typename T>
struct MoveOnlyCallableImpl : MoveOnlyCallable<T> {
    F func;
    explicit MoveOnlyCallableImpl(F&& f) : func(std::move(f)) {}
    void operator()(T val) override { func(std::move(val)); }
};

template <typename T>
class Promise {
public:
    using Callback = std::unique_ptr<MoveOnlyCallable<T>>;

    Promise() = default;

    template<typename F>
    void then(F&& then) {
        callback = std::make_unique<MoveOnlyCallableImpl<F, T>>(std::forward<F>(then));
    }

    void setResolveValue(T val) {
        this->value = std::move(val);
    }

    void resolve() {
        if (callback) {
            (*callback)(std::move(value));
        }
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
    T value;

    std::any context;
};

template<typename T>
class PromiseAll : public std::enable_shared_from_this<PromiseAll<T>> {
public:
    using Callback = std::unique_ptr<MoveOnlyCallable<std::vector<T>>>;

    explicit PromiseAll(std::vector<std::shared_ptr<Promise<T>>> promises)
            : promises(std::move(promises)), resolvedCount(0), results(this->promises.size()) {}

    template<typename F>
    void then(F&& then) {
        callback = std::make_unique<MoveOnlyCallableImpl<F, std::vector<T>>>(std::forward<F>(then));
        auto self = this->shared_from_this();
        for (size_t i = 0; i < promises.size(); ++i) {
            if (context.has_value()) {
                promises[i]->setContext(context);
            }

            promises[i]->then([self, i](T val) {
                self->results[i] = std::move(val);
                if (++self->resolvedCount == self->promises.size() && self->callback) {
                    (*(self->callback))(std::move(self->results));
                }
            });
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
    std::vector<std::shared_ptr<Promise<T>>> promises;
    std::vector<T> results;
    size_t resolvedCount;
    Callback callback;

    std::any context;
};

#endif //SPLATOON_SERVER_PROMISE_HPP
