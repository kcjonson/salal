#pragma once

// A value, or the error that kept it from being made: the part of C++23's std::expected that app
// uses, for a salal that is C++20. The members keep std::expected's names, so moving to the standard
// type is a change of spelling (Expected to std::expected, Unexpected to std::unexpected) and nothing
// else. value() and operator* on an error throw std::bad_variant_access.

#include <utility>
#include <variant>

namespace app {

	template <typename E>
	class Unexpected {
	  public:
		explicit Unexpected(E reason)
			: failure(std::move(reason)) {}

		const E& error() const { return failure; }

	  private:
		E failure;
	};

	// NOLINTBEGIN(readability-identifier-naming): std::expected's names
	template <typename T, typename E>
	class Expected {
	  public:
		Expected(T value) // NOLINT(google-explicit-constructor): a value converts, as it does to std::expected
			: state(std::in_place_index<0>, std::move(value)) {}
		Expected(Unexpected<E> failure) // NOLINT(google-explicit-constructor)
			: state(std::in_place_index<1>, failure.error()) {}

		bool	 has_value() const { return state.index() == 0; }
		explicit operator bool() const { return has_value(); }

		T&		 value() & { return std::get<0>(state); }
		const T& value() const& { return std::get<0>(state); }
		T&&		 value() && { return std::get<0>(std::move(state)); }
		const E& error() const { return std::get<1>(state); }

		T&		 operator*() & { return value(); }
		const T& operator*() const& { return value(); }
		T&&		 operator*() && { return std::move(*this).value(); }
		T*		 operator->() { return &value(); }
		const T* operator->() const { return &value(); }

		template <typename U>
		T value_or(U&& fallback) const& {
			return has_value() ? value() : static_cast<T>(std::forward<U>(fallback));
		}

	  private:
		std::variant<T, E> state;
	};
	// NOLINTEND(readability-identifier-naming)

} // namespace app
