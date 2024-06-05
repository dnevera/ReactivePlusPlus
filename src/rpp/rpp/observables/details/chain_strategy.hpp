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
        template<typename T>
        struct internal_operator_traits {

            template<typename TT>
            constexpr decltype(auto) operator|(const internal_operator_traits<TT>&) {
                return std::declval<typename T::template operator_traits<typename TT::value_type>::result_type>();
            }

            template<typename TT>
            constexpr decltype(auto) operator|(const TT&) {
                return std::declval<typename T::template operator_traits<TT>::result_type>();
            }
        };

        template<typename...Types>
        struct resulting_type {
            using value_type = std::decay_t<decltype((details::internal_operator_traits<Types>{} | ... ))>;
        };

        template<typename Type>
        struct resulting_type<Type> {
            using value_type = typename Type::value_type;
        };

        template<typename Tuple, size_t ...I>
        resulting_type<std::tuple_element_t<std::tuple_size_v<Tuple>  - ((I,...)+1) + I, Tuple>...>::value_type make_resulting_type(std::index_sequence<I...>  v);

        template<typename Tuple, size_t ...I>
        std::tuple<decltype(make_resulting_type<Tuple>( std::make_index_sequence<std::tuple_size_v<Tuple> - I>()))...> calculate_resulting_types(std::index_sequence<I...>);
    }
    template<typename... TStrategies>
        requires (sizeof...(TStrategies) > 1)
    class observable_chain_strategy
    {
    public:
        template<typename... TT>
            requires (sizeof...(TStrategies) > 1)
        friend class observable_chain_strategy;

        using strategies_tuple = std::tuple<TStrategies...>;
        using types_chain                  = decltype(details::calculate_resulting_types<strategies_tuple>(std::make_index_sequence<sizeof...(TStrategies)>()));
        using value_type                   = std::tuple_element_t<0, types_chain>;
        using expected_disposable_strategy = details::observables::fixed_disposable_strategy_selector<0>; // details::observables::deduce_updated_disposable_strategy<TStrategy, typename base::expected_disposable_strategy>;

        observable_chain_strategy(const TStrategies&... strategies)
            : m_strategies(strategies...)
        {
        }

        template<typename TStrategy, typename ...TRest>
        observable_chain_strategy(const TStrategy& strategy, const observable_chain_strategy<TRest...>& strategies)
            : m_strategies(strategies.m_strategies.apply([](const TStrategy& strategy, const TRest& ...r) { return rpp::utils::tuple{strategy, r...}; }, strategy))
        {
        }

        template<rpp::constraint::observer Observer>
        void subscribe(Observer&& observer) const
        {
            [[maybe_unused]] const auto drain_on_exit = own_current_thread_if_needed();
            m_strategies.apply(&apply<Observer>, std::forward<Observer>(observer));
        }
    private:
        template<size_t I, typename...>
        struct internal_piping;

        template<size_t I, rpp::constraint::observer_of_type<std::tuple_element_t<I, types_chain>> TObserver, rpp::constraint::operator_subscribe<rpp::utils::extract_observer_type_t<TObserver>> TStrategy>
        struct internal_piping<I, TObserver, TStrategy>
        {
            using TRestStrategies = std::tuple_element_t<I+1, strategies_tuple>;

            TObserver observer;
            TStrategy strategy;

            constexpr auto operator|(const TRestStrategies& rest) &&
            {
                std::move(strategy).subscribe(std::move(observer), rest);
            }
        };

        template<size_t I, rpp::constraint::observer_of_type<std::tuple_element_t<I, types_chain>> TObserver>
        struct internal_piping<I, TObserver>
        {
            using TStrategy = std::tuple_element_t<I, strategies_tuple>;
            TObserver observer;

            constexpr auto operator|(const TStrategy& strategy) &&
            {
                if constexpr (I + 1 < sizeof...(TStrategies)) {
                    if constexpr (rpp::constraint::operator_lift_with_disposable_strategy<TStrategy, std::tuple_element_t<I+1, types_chain>, typename base::expected_disposable_strategy>)
                        return internal_piping<I+1, decltype(strategy.template lift_with_disposable_strategy<std::tuple_element_t<I+1, types_chain>, typename base::expected_disposable_strategy>(std::move(observer)));
                    else if constexpr (rpp::constraint::operator_lift<TStrategy, std::tuple_element_t<I+1, types_chain>>)
                        return internal_piping<I+1, decltype(strategy.template lift<std::tuple_element_t<I+1, types_chain>>(std::move(observer)))>{strategy.template lift<std::tuple_element_t<I+1, types_chain>>(std::move(observer))};
                    else
                        return internal_piping<I, TObserver, std::decay_t<TStrategy>>{std::move(observer), strategy};
                }
                else
                    return strategy.subscribe(std::move(observer));
            }
        };

        template<typename Observer>
        static void apply(Observer&& observer, const TStrategies&... strategies) {
            (internal_piping<0, std::decay_t<Observer>>{std::forward<Observer>(observer)} | ... | strategies);
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
        requires (!rpp::constraint::operator_subscribe<New, typename observable_chain_strategy<Args...>::value_type>)
    struct make_chain_observable<New, observable_chain_strategy<Args...>>
    {
        using type = observable_chain_strategy<New, Args...>;
    };

    template<typename New, typename Old>
    using make_chain_observable_t = typename make_chain_observable<New, Old>::type;
} // namespace rpp
