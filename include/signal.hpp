/*
MIT License

Copyright (c) 2026 dakingffo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <iostream>
#if defined(_MSC_VER) && _MSC_VER > 1000 || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
#pragma once
#endif

#ifndef DAKING_SIGNAL_HPP
#define DAKING_SIGNAL_HPP

#ifndef DAKING_ALWAYS_INLINE
#   if defined(_MSC_VER)
#       define DAKING_ALWAYS_INLINE [[msvc::forceinline]]
#   else
#       define DAKING_ALWAYS_INLINE [[gnu::always_inline]] 
#   endif
#endif // !DAKING_ALWAYS_INLINE

#include <stdexec/execution.hpp>
#include <exec/async_scope.hpp>
#include <vector>
#include <algorithm>
#include <memory>

namespace daking {
    namespace detail {
        template <typename Arg>
        concept signal_arg = std::copyable<Arg>;

        template <typename...Args>
        struct signal;

        template <signal_arg...Args>
        struct signal<Args...> {
            using base = signal<Args...>;
            static constexpr bool is_void_signal = false;

            template <typename... T>
                requires (sizeof...(T) == sizeof...(Args))
            signal(T&&... value) : args_{std::forward<T>(value)...} {}
            std::tuple<Args...> args_;
        };

        template <>
        struct signal<void> {
            using base = signal<void>;

            static constexpr bool is_void_signal = true;
        };

        inline constexpr auto signal_cast = []<typename... Args>(signal<Args...>*) consteval -> signal<Args...>  {
            return {};
        };

        template <typename T, typename = void>
        struct signal_degradation {
            using type = void;
        };

        template <typename T>
        struct signal_degradation<T, std::void_t<decltype(signal_cast(std::declval<T*>()))>> {
            using type = decltype(signal_cast(std::declval<T*>()));
        };

        template <typename S>
        using signal_degradation_t = typename signal_degradation<S>::type;

        template <typename S>
        concept emittable = (!std::same_as<signal_degradation_t<S>, void>);

        template <emittable Signal>
        struct emitter_unit;

        struct emitter_scope;

        template <emittable... Signals>
        struct emitter_impl;

        template <typename E>
        concept emitter = std::derived_from<E, emitter_scope>;

        template <emittable Signal>
        struct slot_base;

        template <emittable Signal, typename SenderClosure>
        struct slot_impl;

        template <emittable Signal>
        struct connect_t;

        template <emittable Signal>
        struct disconnect_t;

        struct emit_t;

        struct broadcast_t{};

        struct capture_t{/*...*/};

        template <emittable Signal, typename SenderClosure>
        struct connection_signatures;

        template <typename Connection, emittable Signal>
        struct is_connection_signatures {
            static constexpr bool value = false;
        };

        template <emittable Signal, typename SenderClosure>
        struct is_connection_signatures<connection_signatures<Signal, SenderClosure>, Signal> {
            static constexpr bool value = true;
        };

        template <typename Connection, emittable Signal>
        inline constexpr bool is_connection_signatures_v = is_connection_signatures<Connection, Signal>::value;

        template <typename Connection, typename Signal>
        concept connection = is_connection_signatures_v<Connection, Signal>;

        template <emittable Signal, typename SenderClosure>
        struct connection_signatures {
            using weak_slot = std::weak_ptr<slot_base<signal_degradation_t<Signal>>>;

            DAKING_ALWAYS_INLINE bool enable() const noexcept {
                auto slot = ptr_.lock();
                if (!slot) {
                    return false;
                }
                slot->enabled_.store(true, std::memory_order_release);
                return true;
            }

            DAKING_ALWAYS_INLINE bool disable() const noexcept {
                auto slot = ptr_.lock();
                if (!slot) {
                    return false;
                }
                slot->enabled_.store(false, std::memory_order_release);
                return true;
            }

        private:
            friend struct emitter_unit<Signal>;
            friend struct connect_t<Signal>;
            friend struct disconnect_t<Signal>;
            friend struct emit_t;

            connection_signatures(weak_slot&& ptr, exec::async_scope* scope) 
                : ptr_(std::move(ptr)), scope_(scope) {}
            
            weak_slot          ptr_;
            exec::async_scope* scope_;
        };

        template <emittable Signal>
        struct connect_t {
        public:
            template <std::derived_from<emitter_unit<Signal>> E, typename SenderClosure>
                requires (std::copy_constructible<SenderClosure> &&
                    (!Signal::is_void_signal && std::derived_from<SenderClosure, stdexec::sender_adaptor_closure<SenderClosure>> 
                    || Signal::is_void_signal && stdexec::sender<SenderClosure>))
            DAKING_ALWAYS_INLINE 
            connection_signatures<Signal, SenderClosure> operator()(E* emitter, SenderClosure&& sender_closure) const {
                return Impl<SenderClosure>(emitter, &emitter->scope_, std::forward<SenderClosure>(sender_closure));
            }

            template <std::derived_from<emitter_unit<Signal>> E, typename SenderClosure>
                requires (std::copy_constructible<SenderClosure> &&
                    (!Signal::is_void_signal && std::derived_from<SenderClosure, stdexec::sender_adaptor_closure<SenderClosure>> 
                    || Signal::is_void_signal && stdexec::sender<SenderClosure>))
            DAKING_ALWAYS_INLINE 
            connection_signatures<Signal, SenderClosure> operator()(E& emitter, SenderClosure&& sender_closure) const {
                return Impl<SenderClosure>(&emitter, &emitter.scope_, std::forward<SenderClosure>(sender_closure));
            }

        private:
            template <typename SenderClosure>
            DAKING_ALWAYS_INLINE 
            static connection_signatures<Signal, SenderClosure> Impl(
                emitter_unit<Signal>* emitter, exec::async_scope* scope, SenderClosure&& sender_closure) {
                    return {emitter->Register(std::forward<SenderClosure>(sender_closure)), scope};
            }
        };

        template <emittable Signal>
        struct disconnect_t {
        public:
            template <typename SenderClosure>
                requires (!Signal::is_void_signal && std::copy_constructible<SenderClosure> 
                    && std::derived_from<SenderClosure, stdexec::sender_adaptor_closure<SenderClosure>> 
                    || Signal::is_void_signal && stdexec::sender<SenderClosure>)
            DAKING_ALWAYS_INLINE 
            bool operator()(emitter_unit<Signal>* emitter, connection_signatures<Signal, SenderClosure>& con) const {
                return Impl(emitter, con);
            }

            template <typename SenderClosure>
                requires (!Signal::is_void_signal && std::copy_constructible<SenderClosure> 
                    && std::derived_from<SenderClosure, stdexec::sender_adaptor_closure<SenderClosure>> 
                    || Signal::is_void_signal && stdexec::sender<SenderClosure>)
            DAKING_ALWAYS_INLINE 
            bool operator()(emitter_unit<Signal>& emitter, connection_signatures<Signal, SenderClosure>& con) const {
                return Impl(&emitter, con);
            }

        private:
            template <typename SenderClosure>
            DAKING_ALWAYS_INLINE 
            static bool Impl(emitter_unit<Signal>* emitter, connection_signatures<Signal, SenderClosure>& con) {
                auto slot = con.ptr_.lock();
                if (!slot) {
                    return false;
                }
                else {
                    return emitter->Unregister(std::move(slot));
                }
            }
        };

        struct emit_t {
        public:
            template <emittable Signal, std::derived_from<emitter_unit<Signal>> Emitter>
            DAKING_ALWAYS_INLINE void operator()(const Signal& signal, broadcast_t, Emitter* emitter) const {
                Broadcast<Signal>(signal, emitter, &emitter->scope_);
            }

            template <emittable Signal, std::derived_from<emitter_unit<Signal>> Emitter>
            DAKING_ALWAYS_INLINE void operator()(const Signal& signal, broadcast_t, Emitter& emitter) const {
                this->operator()(signal, broadcast_t{}, &emitter);
            }

            template <emitter Emitter>
            DAKING_ALWAYS_INLINE auto operator()(broadcast_t, Emitter* emitter) const {
                return broadcast_emitter_closure<Emitter>{emitter, &emitter->scope_};
            }

            template <emitter Emitter>
            DAKING_ALWAYS_INLINE auto operator()(broadcast_t , Emitter& emitter) const {
                return this->operator()(broadcast_t{}, &emitter);
            }

            template <emittable Signal, std::derived_from<emitter_unit<Signal>> Emitter, typename...SenderClosures>
                requires (sizeof...(SenderClosures) > 0)
            DAKING_ALWAYS_INLINE auto operator()(const Signal& signal, capture_t, 
                Emitter* emitter, const connection_signatures<Signal, SenderClosures>&... cons) const {
                return Capture<Signal>(signal, emitter, &emitter->scope_, cons...);
            }

            template <emittable Signal, std::derived_from<emitter_unit<Signal>> Emitter, typename...SenderClosures>
                requires (sizeof...(SenderClosures) > 0)
            DAKING_ALWAYS_INLINE auto operator()(const Signal& signal, capture_t,
                 Emitter& emitter, const connection_signatures<Signal, SenderClosures>&... cons) const {
                return this->operator()(signal, capture_t{}, &emitter, cons...);
            }

            template <emittable Signal, std::derived_from<emitter_unit<Signal>> Emitter, typename...SenderClosures>
                requires (sizeof...(SenderClosures) > 0)
            DAKING_ALWAYS_INLINE auto operator()(capture_t, 
                Emitter* emitter, const connection_signatures<Signal, SenderClosures>&... cons) const {
                return capture_emitter_closure<Signal, SenderClosures...>{emitter, &emitter->scope_, {cons...}};
            }

            template <emittable Signal, std::derived_from<emitter_unit<Signal>> Emitter, typename...SenderClosures>
                requires (sizeof...(SenderClosures) > 0)
            DAKING_ALWAYS_INLINE auto operator()(capture_t,
                Emitter& emitter, const connection_signatures<Signal, SenderClosures>&... cons) const {
                return this->operator()(capture_t{}, &emitter, cons...);
            }

            template <emittable Signal, typename...SenderClosures>
                requires (sizeof...(SenderClosures) > 0)
            DAKING_ALWAYS_INLINE void operator()(const Signal& signal, const connection_signatures<Signal, SenderClosures>&... cons) const {
                EmitConnection<Signal>(signal, cons...);
            }

            template <emittable Signal, typename...SenderClosures>
                requires (sizeof...(SenderClosures) > 0)
            DAKING_ALWAYS_INLINE auto operator()(const connection_signatures<Signal, SenderClosures>&... cons) const {
                return connection_closure<Signal, SenderClosures...>({cons...});
            }

        private:
            template <stdexec::sender WhenAllSender, stdexec::receiver Receiver>
            struct specific_emission_operation_state {
                using inner_operation = stdexec::connect_result_t<WhenAllSender, Receiver>;
                union {
                    inner_operation op_;
                    Receiver        rcvr_;
                };
                std::exception_ptr error_ = nullptr;

                template <stdexec::sender...FutureSenders, stdexec::receiver Rcvr>
                specific_emission_operation_state(std::tuple<std::optional<FutureSenders>...>&& futures, Rcvr&& rcvr) {
                    new (std::addressof(op_)) inner_operation(
                        std::apply([&rcvr](std::optional<FutureSenders>&&... futures) {
                            return stdexec::connect(stdexec::when_all(std::move(futures).value()...), std::forward<Rcvr>(rcvr));
                        }, std::move(futures))
                    );
                }

                template <stdexec::receiver Rcvr>
                specific_emission_operation_state(std::exception_ptr&& e, Rcvr&& rcvr) {
                    new (std::addressof(rcvr_)) Receiver(std::forward<Rcvr>(rcvr));
                    error_ = std::move(e);
                }

                ~specific_emission_operation_state() {
                    if (error_) {
                        std::addressof(rcvr_)->~Receiver();
                    }
                    else {
                        std::addressof(op_)->~inner_operation();
                    }
                }

                friend void tag_invoke(stdexec::start_t, specific_emission_operation_state& self) noexcept {
                    if (self.error_) {
                        stdexec::set_error(self.rcvr_, self.error_);
                    }
                    else {
                        stdexec::start(self.op_);
                    }
                }
            };

            template <emittable Signal, typename...SenderClosures>
            struct specific_emission_sender;

            template <signal_arg...Args, typename...SenderClosures>
            struct specific_emission_sender<signal<Args...>, SenderClosures...> {
                using sender_concept = stdexec::sender_t;
                template <typename SenderClosure>
                using future_sender = std::decay_t<decltype(std::declval<exec::async_scope>().spawn_future(stdexec::just(std::declval<Args>()...) | std::declval<SenderClosure&&>()))>;
                using when_all_sender = std::decay_t<decltype(stdexec::when_all(std::declval<future_sender<SenderClosures>>()...))>;
                using completion_signatures = stdexec::transform_completion_signatures<
                    stdexec::completion_signatures_of_t<when_all_sender>,
                    stdexec::completion_signatures<stdexec::set_error_t(std::exception_ptr)>
                    >;

                template <stdexec::receiver Receiver>
                friend auto tag_invoke(stdexec::connect_t, specific_emission_sender&& self, Receiver&& rcvr) {
                    if (self.error_) {
                        return specific_emission_operation_state<when_all_sender, std::decay_t<Receiver>>(
                            std::move(self.error_), std::forward<Receiver>(rcvr)
                        );
                    }
                    else {
                        return specific_emission_operation_state<when_all_sender, std::decay_t<Receiver>>(
                            std::move(self.futures_), std::forward<Receiver>(rcvr)
                        );
                    }
                }

            private:
                friend struct emit_t;

                template <std::size_t N>
                auto At() noexcept {
                    return &std::get<N>(futures_);
                }

                void Emplace_error(const std::runtime_error& e) {
                    if (!error_) {
                        error_ = std::make_exception_ptr(e);
                    }
                }

                std::tuple<std::optional<future_sender<SenderClosures>>...> futures_;
                std::exception_ptr error_ = nullptr;
            };

            template <stdexec::sender... Senders>
            struct specific_emission_sender<signal<void>, Senders...> {
                using sender_concept = stdexec::sender_t;
                template <typename Sender>
                using future_sender = std::decay_t<decltype(std::declval<exec::async_scope>().spawn_future(std::declval<Sender&&>()))>;
                using when_all_sender = std::decay_t<decltype(stdexec::when_all(std::declval<future_sender<Senders>>()...))>;
                using completion_signatures = stdexec::transform_completion_signatures<
                    stdexec::completion_signatures_of_t<when_all_sender>,
                    stdexec::completion_signatures<stdexec::set_error_t(std::exception_ptr)>
                    >;

                specific_emission_sender()  = default;
                ~specific_emission_sender() = default;

                specific_emission_sender(const specific_emission_sender&)            = delete;
                specific_emission_sender(specific_emission_sender&&)                 = default;
                specific_emission_sender& operator=(const specific_emission_sender&) = delete;
                specific_emission_sender& operator=(specific_emission_sender&&)      = delete;

                template <stdexec::receiver Receiver>
                friend auto tag_invoke(stdexec::connect_t, specific_emission_sender&& self, Receiver&& rcvr) {
                    if (self.error_) {
                        return specific_emission_operation_state<when_all_sender, std::decay_t<Receiver>>(
                            std::move(self.error_), std::forward<Receiver>(rcvr)
                        );
                    }
                    else {
                        return specific_emission_operation_state<when_all_sender, std::decay_t<Receiver>>(
                            std::move(self.futures_), std::forward<Receiver>(rcvr)
                        );
                    }
                }

            private:
                friend struct emit_t;

                template <std::size_t N>
                auto At() noexcept {
                    return &std::get<N>(futures_);
                }
                
                void Emplace_error(const std::runtime_error& e) {
                    if (!error_) {
                        error_ = std::make_exception_ptr(e);
                    }
                }

                std::tuple<std::optional<future_sender<Senders>>...> futures_;
                std::exception_ptr error_ = nullptr;
            };

            template <emitter Emitter>
            struct broadcast_emitter_closure {
                Emitter*           emitter_;
                exec::async_scope* scope_;

                template <emittable Signal>
                    requires std::derived_from<Emitter, emitter_unit<Signal>>
                friend void operator>>(const Signal& signal, broadcast_emitter_closure&& self) {
                    emit_t::Broadcast<Signal>(signal, self.emitter_, self.scope_);
                }
            };

            template <emittable Signal, typename...SenderClosures>
            struct capture_emitter_closure {
                emitter_unit<Signal>* emitter_;
                exec::async_scope*    scope_;
                std::tuple<connection_signatures<Signal, SenderClosures>...> cons_;

                friend auto operator>>(const Signal& signal, capture_emitter_closure&& self) {
                    return std::apply([&](connection_signatures<Signal, SenderClosures>&&... cons) {
                        return emit_t::Capture<Signal>(signal, self.emitter_, self.scope_, cons...);
                    }, std::move(self.cons_));
                }
            };

            template <emittable Signal, typename...SenderClosures>
            struct connection_closure {
                std::tuple<connection_signatures<Signal, SenderClosures>...> cons_;

                friend auto operator>>(const Signal& signal, connection_closure&& self) {
                    return std::apply([&](auto&&...cons) { 
                        return emit_t::EmitConnection<Signal>(signal, std::move(cons)...); 
                    }, std::move(self.cons_));
                }
            };

            template <emittable Signal>
            DAKING_ALWAYS_INLINE static void Broadcast(const Signal& signal, emitter_unit<Signal>* emitter, exec::async_scope* scope) {
                if constexpr (Signal::is_void_signal) {
                    emitter->Broadcast(scope);
                }
                else {
                    std::apply([&](const auto&...args){
                        emitter->Broadcast(scope, args...);
                    }, signal.args_);
                }
            }

            template <emittable Signal, typename...SenderClosures>
            DAKING_ALWAYS_INLINE static auto Capture(const Signal& signal, 
                emitter_unit<Signal>* emitter, exec::async_scope* scope, const connection_signatures<Signal, SenderClosures>&... cons) {
                
                specific_emission_sender<signal_degradation_t<Signal>, SenderClosures...> sender;

                try {
                    emitter->Check(cons...);
                    sender = EmitConnection(signal, cons...);
                    (cons.disable(),...);
                    Broadcast(signal, emitter, scope);
                    (cons.enable(),...);
                }
                catch(std::runtime_error e) {
                    sender.Emplace_error(e);
                }

                return sender;
            }

            template <emittable Signal, typename...SenderClosures>
            DAKING_ALWAYS_INLINE static auto EmitConnection(const Signal& signal, const connection_signatures<Signal, SenderClosures>&... cons) {
                specific_emission_sender<signal_degradation_t<Signal>, SenderClosures...> sender;

                auto get_futures = [&]<std::size_t...Is>(std::index_sequence<Is...>, auto&...cons) {
                    auto get_future = [&]<std::size_t I>(auto& con) {
                        auto slot = con.ptr_.lock();
                        if (slot) {
                            if (slot->enabled_.load(std::memory_order_acquire)) {
                                if constexpr (Signal::is_void_signal) {
                                    slot->Invoke(con.scope_, sender.template At<I>());
                                }
                                else {
                                    std::apply([&](auto&...args) { slot->Invoke(con.scope_, sender.template At<I>(), args...); }, signal.args_);
                                }
                                return ;
                            }
                            sender.Emplace_error(std::runtime_error("Can't create sender: the connection has been disabled."));
                        }
                        else {
                            sender.Emplace_error(std::runtime_error("Can't create sender: the connection has been closed."));
                        }
                    };
                    (get_future.template operator()<Is>(cons), ...);
                };

                get_futures(std::make_index_sequence<sizeof...(SenderClosures)>(), cons...);

                return sender;
            }
        };

        template <emittable Signal>
        struct slot_base;

        template <signal_arg...Args>
        struct slot_base<signal<Args...>> {
            slot_base()          = default;
            virtual ~slot_base() = default;

            virtual void Invoke(exec::async_scope* scope, void* sender, const Args&...args) = 0;

            std::atomic_bool enabled_ = true;
        };

        template <>
        struct slot_base<signal<void>> {
            slot_base()          = default;
            virtual ~slot_base() = default;

            virtual void Invoke(exec::async_scope* scope, void* sender) = 0;

            std::atomic_bool enabled_ = true;
        };

        template <emittable Signal, typename SenderClosure>
        struct slot_impl;

        template <typename...Args, typename SenderClosure>
            requires (!signal<Args...>::is_void_signal && std::copy_constructible<SenderClosure> 
                && std::derived_from<SenderClosure, stdexec::sender_adaptor_closure<SenderClosure>>)
        struct slot_impl<signal<Args...>, SenderClosure> : slot_base<signal<Args...>> {
            template <typename C>
            slot_impl(C&& closure) : closure_(std::forward<C>(closure)) {}
            ~slot_impl() = default;
            
            void Invoke(exec::async_scope* scope, void* sender, const Args&...args) override {
                if (this->enabled_.load(std::memory_order_acquire)) {
                    if (sender) {
                        auto future_sender = scope->spawn_future(
                            stdexec::just(args...) | closure_
                        );
                        auto target = (std::optional<decltype(future_sender)>*)sender;
                        *target = std::move(future_sender);
                    }
                    else {
                        scope->spawn(
                            stdexec::just(args...) | closure_ | stdexec::then([](auto&&...) noexcept {}) 
                        );
                    }
                }
            }

            SenderClosure closure_;
        };

        template <emittable Signal, stdexec::sender Sender>
            requires (Signal::is_void_signal)
        struct slot_impl<Signal, Sender> : slot_base<signal_degradation_t<Signal>> {
            template <stdexec::sender S>
            slot_impl(S&& sender) : sender_(std::forward<S>(sender)) {}
            ~slot_impl() = default;
            
            void Invoke(exec::async_scope* scope, void* sender) override {
                if (this->enabled_.load(std::memory_order_acquire)) {
                    if (sender) {
                        auto future_sender = scope->spawn_future(
                            sender_
                        );
                        auto target = (std::optional<decltype(future_sender)>*)sender;
                        *target = std::move(future_sender);
                    }
                    else {
                        scope->spawn(
                            sender_ | stdexec::then([](auto&&...) noexcept {})
                        );
                    }
                }
            }

            Sender sender_;
        };

        template <emittable Signal>
        struct emitter_unit {
            using slot = std::shared_ptr<slot_base<signal_degradation_t<Signal>>>;

            emitter_unit()  = default;
            ~emitter_unit() = default;

        private:
            friend struct connect_t<Signal>;
            friend struct disconnect_t<Signal>;
            friend struct emit_t;

            template <typename SenderClosure>
            std::weak_ptr<slot_base<signal_degradation_t<Signal>>> Register(SenderClosure&& sender_closure) {
                slot new_slot = std::make_shared<slot_impl<signal_degradation_t<Signal>, 
                    std::decay_t<SenderClosure>>>(std::forward<SenderClosure>(sender_closure));

                std::shared_ptr<std::vector<slot>> old_slots = slots_.load(std::memory_order_acquire);
                std::shared_ptr<std::vector<slot>> new_slots;

                do {
                    if (old_slots) [[likely]] {
                        new_slots = std::make_shared<std::vector<slot>>(*old_slots);
                    } else {
                        new_slots = std::make_shared<std::vector<slot>>();
                    }
                    new_slots->push_back(new_slot);
                } while (!slots_.compare_exchange_weak(
                            old_slots, new_slots,
                            std::memory_order_release, 
                            std::memory_order_acquire
                        ));

                return new_slot;
            }

            bool Unregister(std::shared_ptr<slot_base<signal_degradation_t<Signal>>>&& ptr) {
                std::shared_ptr<std::vector<slot>> old_slots = slots_.load(std::memory_order_acquire);
                std::shared_ptr<std::vector<slot>> new_slots;

                do {
                    if (old_slots) [[likely]] {
                        new_slots = std::make_shared<std::vector<slot>>(*old_slots);
                    } else {
                        new_slots = std::make_shared<std::vector<slot>>();
                    }
                    auto it = std::remove(new_slots->begin(), new_slots->end(), ptr);
                    if (it == new_slots->end()) {
                        return false;
                    }
                    else {
                        new_slots->erase(it);
                    }
                } while (!slots_.compare_exchange_weak(
                            old_slots, new_slots,
                            std::memory_order_release, 
                            std::memory_order_acquire
                        ));

                return true;
            }

            template <typename...Args>
            void Broadcast(exec::async_scope* scope, const Args&... args) {
                auto current_slots = slots_.load(std::memory_order_acquire);

                if (current_slots) [[likely]] {
                    for (auto& slot_ptr : *current_slots) {
                        slot_ptr->Invoke(scope, nullptr, args...);
                    }
                }
            }

            template <typename...SenderClosures>
            void Check(const connection_signatures<Signal, SenderClosures>&... cons) {
                auto current_slots = slots_.load(std::memory_order_acquire);
                if (current_slots) [[likely]] {
                    constexpr std::size_t size = []() consteval {
                        std::size_t size = sizeof...(SenderClosures) - 1;
                        for (int i = 1; i <= 32; i <<= 1)
                            size |= size >> i;
                        return size + 1;
                    }();
                    std::cout << "s:" << size << std::endl;
                    void* hash_table[size] = {nullptr};
                    std::hash<void*> hasher{};

                    auto make_hash_table = [&](const auto& con) {
                        auto ptr = con.ptr_.lock();
                        if (!ptr) {
                            throw std::runtime_error("Can't create sender: the connection has been closed.");
                        }
                        auto p = (void*)(ptr.get());
                        auto hash_idx = hasher(p) & (size - 1);
                        while (hash_table[hash_idx] != nullptr) {
                            hash_idx = (hash_idx + 1) & (size - 1); 
                        }
                        hash_table[hash_idx] = p;
                    };
                    (make_hash_table(cons), ...);

                    std::size_t count = 0;
                    for (auto& slot_ptr : *current_slots) {
                        auto p = (void*)(slot_ptr.get());
                        auto hash_idx = hasher(p) & (size - 1);
                        for (int i = 0; i < size && hash_table[hash_idx]; i++) {
                            if (hash_table[hash_idx] == p) {
                                count++;
                                break;
                            }
                            else {
                                hash_idx = (hash_idx + 1) & (size - 1); 
                            }
                        }
                    }

                    if (count == sizeof...(SenderClosures)) {
                        return;
                    }
                }

                throw std::runtime_error("Can't create sender: the connection is not connected to the emmiter or there are the same connections.");
            }

            std::atomic<std::shared_ptr<std::vector<slot>>> slots_;
        };

        struct emitter_scope {
            emitter_scope() = default;
            ~emitter_scope() {
                stdexec::sync_wait(scope_.on_empty()); 
            }

            exec::async_scope scope_;
        };

        template <emittable... Signals>
        struct emitter_impl : emitter_unit<Signals>..., virtual emitter_scope {
            friend struct emit_t;

            static_assert(sizeof...(Signals) > 0, "Emitter should at least emit one kind of signal.");
        };
    }

    using detail::signal;
    using detail::emittable;
    using detail::emitter;
    using detail::connection;

    template <emittable Signal>
    inline constexpr detail::connect_t<Signal> connect;
    template <emittable Signal>
    inline constexpr detail::disconnect_t<Signal> disconnect;

    inline constexpr detail::emit_t      emit;
    inline constexpr detail::broadcast_t broadcast;
    inline constexpr detail::capture_t   capture;

    template <emittable... Signals>
    using enable_signal = detail::emitter_impl<Signals...>;
}

#endif // !DAKING_SIGNAL_HPP