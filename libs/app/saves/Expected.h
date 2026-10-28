#pragma once

// A value, or the error that kept it from being made: the part of C++23's std::expected that app
// uses, for a salal that is C++20. The members keep std::expected's names, so moving to the standard
// type is a change of spelling (Expected to std::expected, Unexpected to std::unexpected,
// BadExpectedAccess to std::bad_expected_access) and nothing else. value() on an error throws
// BadExpectedAccess, as std::expected's throws bad_expected_access; so does operator*, which on
// std::expected is undefined there.

#include <exception>
#include <utility>
#include <variant>

namespace app {

	class BadExpectedAccess : public std::exception {
	  public:
		const char* what() const noexcept override { return "app::Expected: value() on an error"; }
	};

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

		T& value() & {
			check();
			return std::get<0>(state);
		}
		const T& value() const& {
			check();
			return std::get<0>(state);
		}
		T&& value() && {
			check();
			return std::get<0>(std::move(state));
		}
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
		void check() const {
			if (!has_value()) {
				throw BadExpectedAccess();
			}
		}

		std::variant<T, E> state;
	};
	// NOLINTEND(readability-identifier-naming)

} // namespace app
