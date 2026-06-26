# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2025/08/19 17:59:19 by malkilan          #+#    #+#              #
#    Updated: 2025/09/10 14:47:13 by malkilan         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

NAME		= libft.a

FILENAME	= ft_isalnum.c ft_isprint.c ft_memcmp.c  ft_putchar_fd.c ft_split.c \
					ft_strlcat.c ft_strncmp.c ft_substr.c ft_atoi.c ft_isalpha.c \
					ft_itoa.c ft_memcpy.c  ft_putendl_fd.c ft_strchr.c  ft_strlcpy.c \
					ft_strnstr.c ft_tolower.c ft_bzero.c   ft_isascii.c ft_striteri.c \
					ft_memmove.c ft_putnbr_fd.c  ft_strdup.c  ft_strlen.c  ft_strrchr.c \
					ft_toupper.c ft_calloc.c  ft_isdigit.c ft_memchr.c  ft_memset.c  \
					ft_putstr_fd.c  ft_strjoin.c ft_strmapi.c ft_strtrim.c 
BOUNS		= ft_lstnew_bonus.c ft_lstadd_front_bonus.c ft_lstsize_bonus.c ft_lstlast_bonus.c \
				ft_lstadd_back_bonus.c ft_lstdelone_bonus.c ft_lstclear_bonus.c ft_lstiter_bonus.c
OBJS 		= $(FILENAME:.c=.o)
BOUNS_OBJS	= $(BOUNS:.c=.o)
BONUS_FILE = .bonus.txt

CC = cc
CFLAGS = -Wall -Werror -Wextra

all: $(NAME)

$(NAME): $(OBJS)
	ar rcs $(NAME) $(OBJS)

bonus: $(BONUS_FILE)

$(BONUS_FILE): $(BOUNS_OBJS)
	ar rcs $(NAME) $(BOUNS_OBJS)
	touch $(BONUS_FILE)

clean:
	rm -f $(OBJS) $(BOUNS_OBJS) $(BONUS_FILE)
fclean: clean
	rm -rf $(NAME)

re: fclean all

.PHONY: all clean fclean re bonus