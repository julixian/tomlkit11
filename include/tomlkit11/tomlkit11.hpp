#pragma once

#ifndef TOMLKIT11_TOMLKIT11_HPP
#define TOMLKIT11_TOMLKIT11_HPP

#include "config.hpp"
#include "compat.hpp"
#include "spec.hpp"
#include "error.hpp"
#include "types.hpp"
#include "decor.hpp"
#include "item.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "emitter.hpp"

#if TOMLKIT11_SHOULD_INCLUDE_IMPL
#    include "detail/error.ipp"
#    include "detail/types.ipp"
#    include "detail/decor.ipp"
#    include "detail/item_core.ipp"
#    include "detail/item_access.ipp"
#    include "detail/lexer.ipp"
#    include "detail/parser.ipp"
#    include "detail/emitter.ipp"
#endif

#endif // TOMLKIT11_TOMLKIT11_HPP
