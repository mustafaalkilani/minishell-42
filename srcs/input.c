/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   input.c                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/30 16:46:03 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/30 21:06:15 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "minishell.h"

static char	*read_plain_line(void)
{
	char	*line;
	char	*joined;
	char	buf[2];
	ssize_t	n;

	line = ft_strdup("");
	buf[1] = '\0';
	n = read(STDIN_FILENO, buf, 1);
	while (line && n == 1 && buf[0] != '\n')
	{
		joined = ft_strjoin(line, buf);
		free(line);
		line = joined;
		n = read(STDIN_FILENO, buf, 1);
	}
	if (line && n <= 0 && !*line)
	{
		free(line);
		return (NULL);
	}
	return (line);
}

char	*read_input_line(const char *prompt)
{
	if (isatty(STDIN_FILENO))
		return (readline(prompt));
	return (read_plain_line());
}
