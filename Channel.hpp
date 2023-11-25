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
        self.sender_->send(lhs);
        return std::ranges::empty_view<int>();
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
struct DestroyReceiver {
    void operator()(Receiver<T>* receiver) {
        delete receiver;
    }
};

template <typename T>
using SenderPtr = std::unique_ptr<Sender<T>>;

template <typename T>
using SenderRefPtr = std::shared_ptr<Sender<T>>;

template <typename T>
using ReceiverPtr = std::unique_ptr<Receiver<T>, decltype(DestroyReceiver<T>())>;

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
        return {std::make_unique<Sender<T>>(p),
                ReceiverPtr<T>(new Receiver<T>(p), DestroyReceiver<T>())
        };
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
class Sender final : public std::__range_adaptor_closure<Sender<T>>{
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
class Receiver final {
private:
    friend class DestroyReceiver<T>;

    friend class ReceiverIterator<T>;

private:
    class ReceiverImpl {
    public:
        friend class Channel<T>;

        friend class ReceiverIterator<T>;

        friend class DestroyReceiver<T>;

        explicit ReceiverImpl(std::shared_ptr<Channel<T>> channel)
            : channel_(channel) {

        }

        ~ReceiverImpl() {
            channel_->done();
        }

        ReceiverImpl& operator=(const ReceiverImpl&) = delete;

        ReceiverImpl(const ReceiverImpl&) = delete;

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

private:
    std::shared_ptr<ReceiverImpl> impl_;

private:
    ~Receiver() = default;

public:
    using ValueType = T;
    explicit Receiver(std::shared_ptr<Channel<T>> channel)
        : impl_(std::make_shared<ReceiverImpl>(channel)) {

    }

    Receiver<T>& operator=(const Receiver<T>&) = delete;

    Receiver(const Receiver<T>&) = delete;

    inline auto begin() noexcept -> ReceiverIterator<T> {
        return ReceiverIterator<T>(impl_, ChannelEventType::Unknown);
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
protected:
    std::shared_ptr<typename Receiver<T>::ReceiverImpl> receiverImpl_;
    std::expected<T, ChannelEventType> value_;
private:
    inline auto getValue() noexcept -> std::expected<T, ChannelEventType>& {
        if (!value_.has_value() && value_.error() == ChannelEventType::Unknown) {
            value_ = std::move(receiverImpl_->receive());
        }
        return value_;
    }

public:
    using iterator_category = std::input_iterator_tag;
    using value_type = std::expected<T, ChannelEventType>;

    explicit ReceiverIterator(std::shared_ptr<typename Receiver<T>::ReceiverImpl> receiverImpl,
                              ChannelEventType eventType = ChannelEventType::Unknown)
        : receiverImpl_(receiverImpl)
        , value_(std::unexpected(eventType)) {

    }

    ReceiverIterator(const ReceiverIterator&) = delete;

    ReceiverIterator& operator=(const ReceiverIterator&) = delete;

    ReceiverIterator(ReceiverIterator&& src) noexcept
        : receiverImpl_(std::move(src.receiverImpl_))
        , value_(std::move(src.value_)) {

    }

    inline auto operator=(ReceiverIterator&& src) noexcept -> ReceiverIterator& {
        receiverImpl_ = std::move(src.receiverImpl_);
        value_ = std::move(src.value_);
        return *this;
    }

    inline auto operator*() noexcept -> std::expected<T, ChannelEventType>& {
        return getValue();
    }

    inline auto operator++() noexcept -> ReceiverIterator& {
        getValue();
        value_ = std::move(receiverImpl_->receive());
        return *this;
    }

    inline auto operator==(const ReceiverIterator& iter) noexcept -> bool {
        const auto& lvalue = getValue();
        const auto& rvalue = iter.value_;
        if (lvalue.has_value() && rvalue.has_value()) {
            return &lvalue.value() == &rvalue.value();
        }
        if (lvalue.has_value() || rvalue.has_value()) {
            return false;
        }
        return lvalue.error() == rvalue.error();
    }

    inline auto operator!=(const ReceiverIterator& iter) noexcept -> bool {
        return !operator==(iter);
    }
}; //end of class ReceiverIterator

} //end of namespace Async

#endif //end if ASYNC_CHANNEL