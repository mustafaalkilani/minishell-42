# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#              #
#    Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

NAME        := minishell

CC          := cc
CFLAGS      := -Wall -Wextra -Werror
INCLUDES    := -I includes -I libft -I path-a -I path-b
LDFLAGS     := -lreadline

LIBFT_DIR   := libft
LIBFT       := $(LIBFT_DIR)/libft.a

BUILD_DIR   := build

SRCS := srcs/main.c \
        srcs/utils.c \
        srcs/input.c \
        path-a/lexer/lexer.c \
        path-a/lexer/lexer_utils.c \
        path-a/lexer/lexer_quotes.c \
        path-a/lexer/lexer_ops.c \
        path-a/parser/parser.c \
        path-a/parser/parser_redir.c \
        path-a/parser/parser_utils.c \
        path-a/expander/expander.c \
        path-a/expander/expander_utils.c \
        path-a/expander/expander_split.c \
        path-a/expander/expander_prefix.c \
        path-b/env/env_init.c \
        path-b/env/env_ops.c \
        path-b/env/env_export.c \
        path-b/builtins/builtin_dispatch.c \
        path-b/builtins/builtin_echo.c \
        path-b/builtins/builtin_cd.c \
        path-b/builtins/builtin_pwd.c \
        path-b/builtins/builtin_env.c \
        path-b/builtins/builtin_export.c \
        path-b/builtins/builtin_export_list.c \
        path-b/builtins/builtin_unset.c \
        path-b/builtins/builtin_exit.c \
        path-b/executor/exec_line.c \
        path-b/executor/exec_child.c \
        path-b/executor/exec_path.c \
        path-b/executor/exec_wait.c \
        path-b/redirs/redir_apply.c \
        path-b/redirs/redir_heredoc.c \
        path-b/signals/signals.c \
        path-b/signals/signals_child.c

OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)

all: $(NAME)

$(NAME): $(LIBFT) $(OBJS)
	@$(CC) $(CFLAGS) $(OBJS) $(LIBFT) $(LDFLAGS) -o $@
	@echo "  LD    $@"

$(BUILD_DIR)/%.o: %.c includes/minishell.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@
	@echo "  CC    $<"

$(LIBFT):
	@$(MAKE) -sC $(LIBFT_DIR)

clean:
	@$(MAKE) -sC $(LIBFT_DIR) clean
	@rm -rf $(BUILD_DIR)
	@echo "  RM    $(BUILD_DIR)"

fclean: clean
	@$(MAKE) -sC $(LIBFT_DIR) fclean
	@rm -f $(NAME)
	@echo "  RM    $(NAME)"

re: fclean all

.PHONY: all clean fclean re
