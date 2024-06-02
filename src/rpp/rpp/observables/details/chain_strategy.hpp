//                   ReactivePlusPlus library
//
//           Copyright Aleksey Loginov 2023 - present.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           https://www.boost.org/LICENSE_1_0.txt)
//
//  Project home: https://github.com/victimsnino/ReactivePlusPlus

#pragma once

#include <rpp/observables/fwd.hpp>
#include <rpp/observers/fwd.hpp>

#include <rpp/defs.hpp>
#include <rpp/schedulers/current_thread.hpp>

namespace rpp
{
    namespace details
    {
        template<typename...>
        struct internal_piping;

        template<rpp::constraint::observer TObserver, rpp::constraint::operator_subscribe<rpp::utils::extract_observer_type_t<TObserver>> TStrategy>
        struct internal_piping<TObserver, TStrategy>
        {
            TObserver observer;
            TStrategy strategy;

            template<typename TRestStrategies>
            constexpr auto operator|(TRestStrategies&& rest) &&
            {
                std::move(strategy).subscribe(std::move(observer), std::forward<TRestStrategies>(rest));
            }
        };

        template<rpp::constraint::observer TObserver>
        struct internal_piping<TObserver>
        {
            using value_type = rpp::utils::extract_observer_type_t<TObserver>;

            TObserver observer;

            template<typename TStrategy>
            constexpr auto operator|(TStrategy&& strategy) &&
            {
                // if constexpr (rpp::constraint::operator_lift_with_disposable_strategy<TStrategy, value_type, typename base::expected_disposable_strategy>)
                // m_strategies.subscribe(m_strategy.template lift_with_disposable_strategy<value_type, typename base::expected_disposable_strategy>(std::forward<Observer>(observer)));
                // else
                if constexpr (rpp::constraint::operator_lift<TStrategy, value_type>)
                    return internal_piping<decltype(std::forward<TStrategy>(strategy).template lift<value_type>(std::move(observer)))>{std::forward<TStrategy>(strategy).template lift<value_type>(std::move(observer))};
                else if constexpr (rpp::constraint::operator_subscribe<TStrategy, value_type>)
                    return internal_piping<TObserver, std::decay_t<TStrategy>>{std::move(observer), std::forward<TStrategy>(strategy)};
                else
                    return std::forward<TStrategy>(strategy).subscribe(std::move(observer));
            }
        };
    }
    template<typename... TStrategies>
    class observable_chain_strategy
    {
        // using base = observable_chain_strategy<TStrategies...>;

        // using operator_traits = typename TStrategy::template operator_traits<typename base::value_type>;

    public:
        using expected_disposable_strategy = details::observables::fixed_disposable_strategy_selector<1>; // details::observables::deduce_updated_disposable_strategy<TStrategy, typename base::expected_disposable_strategy>;
        using value_type                   = int; // typename operator_traits::result_type;

        observable_chain_strategy(const TStrategies&... strategies)
            : m_strategies(strategies...)
        {
        }

        template<typename TStrategy, typename ...TRest>
        observable_chain_strategy(const TStrategy& strategy, const observable_chain_strategy<TRest...>& strategies)
            : observable_chain_strategy(strategy, strategies.m_strategies)
        {
        }

        template<rpp::constraint::observer Observer>
        void subscribe(Observer&& observer) const
        {
            [[maybe_unused]] const auto drain_on_exit = own_current_thread_if_needed();
            m_strategies.apply([](auto&& pipe, const TStrategies& ...strategies) {
                (std::forward<decltype(pipe)>(pipe) | ... | strategies);
            }, details::internal_piping<std::decay_t<Observer>>{std::forward<Observer>(observer)});

            // if constexpr (rpp::constraint::operator_lift_with_disposable_strategy<TStrategy, typename base::value_type, typename base::expected_disposable_strategy>)
            //     m_strategies.subscribe(m_strategy.template lift_with_disposable_strategy<typename base::value_type, typename base::expected_disposable_strategy>(std::forward<Observer>(observer)));
            // else if constexpr (rpp::constraint::operator_lift<TStrategy, typename base::value_type>)
            //     m_strategies.subscribe(m_strategy.template lift<typename base::value_type>(std::forward<Observer>(observer)));
            // else
            //     m_strategy.subscribe(std::forward<Observer>(observer), m_strategies);
        }

    private:
        static auto own_current_thread_if_needed()
        {
            // if constexpr (requires { requires operator_traits::own_current_queue; })
                // return rpp::schedulers::current_thread::own_queue_and_drain_finally_if_not_owned();
            // else
                return rpp::utils::none{};
        }

    private:
        RPP_NO_UNIQUE_ADDRESS rpp::utils::tuple<TStrategies...> m_strategies;
    };

    template<typename New, typename Old>
    struct make_chain_observable
    {
        using type = observable_chain_strategy<New, Old>;
    };

    template<typename New, typename... Args>
    struct make_chain_observable<New, observable_chain_strategy<Args...>>
    {
        using type = observable_chain_strategy<New, Args...>;
    };

    template<typename New, typename Old>
    using make_chain_observable_t = typename make_chain_observable<New, Old>::type;
} // namespace rpp
