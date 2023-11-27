//
// Created by Nevermore on 2023/11/25.
//
// Copyright (c) 2023 Nevermore All rights reserved.
//

#ifndef ASYNC_CHANNEL
#define ASYNC_CHANNEL

#include <mutex>
#include <condition_variable>
#include <expected>
#include <memory>
#include <tuple>
#include <list>
#include <concepts>
#include <ranges>
#include <type_traits>
#include <string_view>

namespace Async {

template <typename S>
concept IsString = requires(S&& s) {
    std::forward<S>(s).c_str();
};

template <typename C>
concept IsContainerOrRange = std::ranges::viewable_range<C> &&
    (!IsString<C>) &&
    requires(C&& c) {
        std::forward<C>(c).empty();
        typename decltype(c.begin())::value_type;
    };

template <typename I>
    requires requires(I&&) {
        typename I::element_type::ValueType;
    }
struct GetChannelType {
    using type = I::element_type::ValueType;
};

template <typename T>
concept IsSender = requires(T&& t) {
    typename T::element_type::ValueType;
    std::forward<T>(t)->done();
};

template <typename T>
concept IsReceiver = std::ranges::range<T> && requires(T&& t) {
    typename T::ValueType;
    std::forward<T>(t).tryReceive();
};

template <IsSender S>
struct SenderAdaptorClosure {
private:
    S& sender_;
public:
    constexpr explicit SenderAdaptorClosure(S& sender)
        : sender_(sender) {
    }

    template <std::ranges::viewable_range InputView>
    friend auto operator|(InputView &&lhs, SenderAdaptorClosure<S> self) {
        auto res = self.sender_->send(std::forward<InputView>(lhs));
        return std::ranges::single_view<bool>(res);
    }
};

struct SendView {
    template <IsSender S>
    auto operator()(S& sender) const {
        return SenderAdaptorClosure(sender);
    }
};

inline constexpr SendView SenderView;

template <typename T>
class Sender;

template <typename T>
class Receiver;

template <typename T>
using SenderPtr = std::unique_ptr<Sender<T>>;

template <typename T>
using SenderRefPtr = std::shared_ptr<Sender<T>>;

template <typename T>
using ReceiverPtr = std::unique_ptr<Receiver<T>>;

enum class ChannelEventType {
    Unknown,
    Success,
    Closed,
    None,
};

template <typename T, typename = std::enable_if_t<!std::is_reference_v<T>> >
    requires (std::movable<T> || std::copyable<T>)
class Channel {
public:
    friend class Sender<T>;

    friend class Receiver<T>;

    static auto create() noexcept -> std::tuple<SenderPtr<T>, ReceiverPtr<T>> {
        auto p = std::shared_ptr<Channel<T>>(new Channel<T>(), [](Channel<T>* channel) {
            delete channel;
        });
        return { std::make_unique<Sender<T>>(p), std::make_unique<Receiver<T>>(p) };
    }

    inline auto done() noexcept -> void {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!isClosed_) {
            isClosed_ = true;
            cond_.notify_all();
        }
    }
    [[nodiscard]] inline auto isDone() noexcept -> bool {
        std::lock_guard<std::mutex> lock(mutex_);
        return isClosed_;
    }

    Channel(const Channel&) = delete;

    Channel(Channel&&) = delete;

    Channel& operator=(const Channel&) = delete;

    Channel& operator=(Channel&&) = delete;

private:
    Channel() = default;

    ~Channel() = default;

private:
    bool isClosed_ = false;
    std::list<T> messages_;
    std::mutex mutex_;
    std::condition_variable cond_;
};


template <typename T>
class Sender final {
public:
    using ValueType = T;
    explicit Sender(std::shared_ptr<Channel<T>> channel)
        : channel_(channel) {

    }

    ~Sender() {
        if (!channel_->isClosed_) {
            done();
        }
        channel_.reset();
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U, T>> >
    constexpr auto operator()(U&& v) -> Sender<T>& {
        send(std::forward<U>(v));
        return *this;
    }

    template <typename I, typename = std::enable_if_t<std::movable<T>> >
    inline auto send(const I& box) noexcept -> bool
        requires IsContainerOrRange<I> && std::is_convertible_v<typename decltype(box.begin())::value_type, T>  {
        if (box.empty()) {
            return true;
        }
        std::lock_guard<std::mutex> lock(channel_->mutex_);
        if (!channel_->isClosed_) {
            for (const auto& v : std::views::all(box)) {
                channel_->messages_.push_back(v);
            }
            channel_->cond_.notify_one();
            return true;
        }
        return false;
    }

    template <typename I, typename = std::enable_if_t<std::movable<T>> >
    inline auto send(I&& box) noexcept -> bool
        requires IsContainerOrRange<I> && std::is_convertible_v<typename decltype(box.begin())::value_type, T> {
        if (box.empty()) {
            return true;
        }
        std::lock_guard<std::mutex> lock(channel_->mutex_);
        if (!channel_->isClosed_ && !box.empty()) {
            std::move(box.begin(), box.end(), std::back_inserter(channel_->messages_));
            channel_->cond_.notify_one();
            return true;
        }
        return false;
    }

    template <typename U, typename = std::enable_if_t<std::movable<T> && std::is_convertible_v<T, U>> >
    inline auto send(const U& message) noexcept -> bool {
        std::lock_guard<std::mutex> lock(channel_->mutex_);
        if (!channel_->isClosed_) {
            channel_->messages_.push_back(message);
            channel_->cond_.notify_one();
            return true;
        }
        return false;
    }

    template <typename U, typename = std::enable_if_t<std::movable<T> && std::is_convertible_v<T, U>> >
    inline auto send(U&& message) noexcept -> bool {
        std::lock_guard<std::mutex> lock(channel_->mutex_);
        if (!channel_->isClosed_) {
            channel_->messages_.emplace_back(message);
            channel_->cond_.notify_one();
            return true;
        }
        return false;
    }

    inline auto done() noexcept -> void {
        channel_->done();
    }

    [[nodiscard]] inline auto isDone() noexcept -> bool {
        return channel_->isDone();
    }

private:
    std::shared_ptr<Channel<T>> channel_;
};

constexpr std::string_view kClosedMessage = "can't send message to closed channel.";

template <typename T, typename U>
auto operator<<(const SenderPtr<U>& sender, T&& message) -> const SenderPtr<U>&
    requires std::movable<T> && std::is_convertible_v<T, U> {
    if (!sender->send(std::forward<T>(message))) {
        throw std::runtime_error(kClosedMessage.data());
    }
    return sender;
}

template <typename T, typename U>
auto operator<<(const SenderPtr<U>& sender, const T& message) -> const SenderPtr<U>&
    requires std::copyable<T> && std::is_convertible_v<T, U> {
    if (!sender->send(message)) {
        throw std::runtime_error(kClosedMessage.data());
    }
    return sender;
}

template <typename I, typename U>
auto operator<<(const SenderPtr<U>& sender, const I& box) -> const SenderPtr<U>&
    requires IsContainerOrRange<I> && std::is_convertible_v<typename decltype(box.begin())::value_type, U> {
    if (!sender->send(box)) {
        throw std::runtime_error(kClosedMessage.data());
    }
    return sender;
}

template <typename I, typename U>
auto operator<<(const SenderPtr<U>& sender, I&& box) -> const SenderPtr<U>&
    requires IsContainerOrRange<I> && std::is_convertible_v<typename decltype(box.begin())::value_type, U> {
    if (!sender->send(std::forward<I>(box))) {
        throw std::runtime_error(kClosedMessage.data());
    }
    return sender;
}

template <typename T, typename U>
auto operator<<(const SenderRefPtr<U>& sender, T&& message) -> const SenderRefPtr<U>&
    requires std::movable<T> && std::is_convertible_v<T, U> {
    if (!sender->send(std::forward<T>(message))) {
        throw std::runtime_error(kClosedMessage.data());
    }
    return sender;
}

template <typename T, typename U>
auto operator<<(const SenderRefPtr<U>& sender, const T& message) -> const SenderRefPtr<U>&
    requires std::copyable<U> && std::is_convertible_v<T, U> {
    if (!sender->send(message)) {
        throw std::runtime_error(kClosedMessage.data());
    }
    return sender;
}

template <typename I, typename U>
auto operator<<(const SenderRefPtr<U>& sender, const I& box) -> const SenderRefPtr<U>&
    requires IsContainerOrRange<I> && std::is_convertible_v<typename decltype(box.begin())::value_type, U> {
    if (!sender->send(box)) {
        throw std::runtime_error(kClosedMessage.data());
    }
    return sender;
}

template <typename I, typename U>
auto operator<<(const SenderRefPtr<U>& sender, I&& box) -> const SenderRefPtr<U>&
    requires IsContainerOrRange<I> && std::is_convertible_v<typename decltype(box.begin())::value_type, U> {
    if (!sender->send(std::forward<I>(box))) {
        throw std::runtime_error(kClosedMessage.data());
    }
    return sender;
}

template <typename T>
class ReceiverIterator;

template <typename T>
class ConstReceiverIterator;

template <typename T>
class Receiver final {
private:
    friend class ReceiverIterator<T>;
    friend class ConstReceiverIterator<T>;
private:
    class ReceiverImpl {
    public:
        friend class Channel<T>;

        friend class ReceiverIterator<T>;

        explicit ReceiverImpl(std::shared_ptr<Channel<T>> channel)
            : channel_(channel) {

        }

        ~ReceiverImpl() {
            channel_->done();
        }

        inline auto operator=(const ReceiverImpl& src) noexcept -> ReceiverImpl& {
            channel_ = src.channel_;
        }

        ReceiverImpl(const ReceiverImpl& src) noexcept
            : channel_(src.channel_) {

        }

        inline auto operator=(ReceiverImpl&& src) noexcept -> ReceiverImpl& {
            channel_ = std::move(src.channel_);
        }

        ReceiverImpl(ReceiverImpl&& src) noexcept
            : channel_(std::move(src.channel_)) {

        }

        inline auto receive() noexcept -> std::expected<T, ChannelEventType> {
            std::unique_lock<std::mutex> lock(channel_->mutex_);
            channel_->cond_.wait(lock, [this] {
                return !channel_->messages_.empty() || channel_->isClosed_;
            });
            if (channel_->isClosed_ && channel_->messages_.empty()) {
                return std::unexpected(ChannelEventType::Closed);
            }
            auto value = std::move(channel_->messages_.front());
            channel_->messages_.pop_front();
            return value;
        }

        inline auto tryReceive() noexcept -> std::expected<T, ChannelEventType> {
            std::unique_lock<std::mutex> lock(channel_->mutex_);
            if (channel_->messages_.empty()) {
                ChannelEventType type = ChannelEventType::Unknown;
                if (channel_->isClosed_) {
                    type = ChannelEventType::Closed;
                } else {
                    type = ChannelEventType::None;
                }
                return std::unexpected(type);
            }
            auto value = std::move(channel_->messages_.front());
            channel_->messages_.pop_front();
            return value;
        }

        inline auto tryReceiveAll() -> std::list<T> {
            decltype(channel_->messages_) messages;
            {
                std::lock_guard<std::mutex> lock(channel_->mutex_);
                messages.swap(channel_->messages_);
            }
            return messages;
        }

    private:
        std::shared_ptr<Channel<T>> channel_;
    };//end of class ReceiverImpl

    std::shared_ptr<ReceiverImpl> impl_;

public:
    using ValueType = T;
    explicit Receiver(std::shared_ptr<Channel<T>> channel)
        : impl_(std::make_shared<ReceiverImpl>(channel)) {

    }

    ~Receiver() noexcept = default;

    inline auto operator=(const Receiver<T>& src) -> Receiver<T>& {
        impl_ = src.impl_;
    }

    Receiver(const Receiver<T>& src) noexcept
        : impl_(src.impl_) {

    }

    inline auto operator=(Receiver<T>&& src) noexcept -> Receiver<T>& {
        impl_ = std::move(src.impl_);
    }

    Receiver(Receiver<T>&& src) noexcept
        : impl_(std::move(src.impl_)) {

    }

    inline auto begin() noexcept -> ReceiverIterator<T> {
        auto it = ReceiverIterator<T>(impl_, ChannelEventType::Unknown);
        it.value_ = impl_->receive();
        return it;
    }

    inline auto end() noexcept -> ReceiverIterator<T> {
        return ReceiverIterator<T>(impl_, ChannelEventType::Closed);
    }

    inline auto receive() noexcept -> std::expected<T, ChannelEventType> {
        return impl_->receive();
    }

    inline auto tryReceive() noexcept -> std::expected<T, ChannelEventType> {
        return impl_->tryReceive();
    }

    inline auto tryReceiveAll() noexcept -> std::list<T> {
        return impl_->tryReceiveAll();
    }

}; //end of class Receiver

template <typename T>
class ReceiverIterator {
    friend class Receiver<T>;
protected:
    std::shared_ptr<typename Receiver<T>::ReceiverImpl> receiverImpl_;
    mutable std::expected<T, ChannelEventType> value_;

public:
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    explicit ReceiverIterator(std::shared_ptr<typename Receiver<T>::ReceiverImpl> receiverImpl,
                              ChannelEventType eventType = ChannelEventType::Unknown)
        : receiverImpl_(receiverImpl)
        , value_(std::unexpected(eventType)) {

    }

    ReceiverIterator()
        : receiverImpl_(nullptr)
        , value_(std::unexpected(ChannelEventType::Closed)) {

    }

    template <typename = std::enable_if_t<std::is_copy_constructible_v<T>>>
    explicit ReceiverIterator(const ReceiverIterator& src) noexcept
        : receiverImpl_(src.receiverImpl_)
        , value_(src.value_) {

    }

    template <typename = std::enable_if_t<std::is_copy_assignable_v<T>>>
    ReceiverIterator& operator=(const ReceiverIterator& src) noexcept {
        receiverImpl_ = src.receiverImpl_;
        value_ = src.value_;
        return *this;
    }

    template <typename = std::enable_if_t<std::is_move_constructible_v<T>>>
    explicit ReceiverIterator(ReceiverIterator&& src) noexcept
        : receiverImpl_(std::move(src.receiverImpl_))
        , value_(std::move(src.value_)) {

    }

    template <typename = std::enable_if_t<std::is_move_assignable_v<T>>>
    inline auto operator=(ReceiverIterator&& src) noexcept -> ReceiverIterator& {
        receiverImpl_ = std::move(src.receiverImpl_);
        value_ = std::move(src.value_);
        return *this;
    }

    inline auto operator*() const noexcept -> reference {
        return *value_;
    }

    inline auto operator++() noexcept -> ReceiverIterator& {
        value_ = std::move(receiverImpl_->receive());
        return *this;
    }

    inline auto operator++(int) noexcept -> ReceiverIterator {
        auto temp = std::move(*this);
        *this = ReceiverIterator(receiverImpl_);
        this->value_ = receiverImpl_->receive();
        return temp;
    }

    inline auto operator==(const ReceiverIterator& iter) const noexcept -> bool {
        const auto& lvalue = value_;
        const auto& rvalue = iter.value_;
        if (lvalue.has_value() && rvalue.has_value()) {
            return &lvalue.value() == &rvalue.value();
        }
        if (lvalue.has_value() || rvalue.has_value()) {
            return false;
        }
        return lvalue.error() == rvalue.error();
    }

    inline auto operator!=(const ReceiverIterator& iter) const noexcept -> bool {
        return !operator==(iter);
    }
}; //end of class ReceiverIterator

} //end of namespace Async

#endif //end if ASYNC_CHANNEL