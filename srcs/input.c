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

/* One byte at a time so nothing is buffered past the newline. A shared
** buffer would be read into this process and then be missing from the
** stdin a forked child inherits, for example during `cat << EOF`. */
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

/* readline() gives history and line editing on a terminal, but it echoes
** whatever it reads when stdin is a pipe, which would duplicate every
** command into the output a test harness diffs against bash. */
char	*read_input_line(const char *prompt)
{
	if (isatty(STDIN_FILENO))
		return (readline(prompt));
	return (read_plain_line());
}
