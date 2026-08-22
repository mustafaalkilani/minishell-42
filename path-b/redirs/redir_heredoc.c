/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   redir_heredoc.c                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

/* The delimiter must match the whole line, so "EOF" ends the heredoc but
** "EOFX" or " EOF" do not. */
static int	delim_matches(const char *line, const char *delim)
{
	return (!ft_strncmp(line, delim, ft_strlen(delim) + 1));
}

/* The "> " continuation prompt only belongs on a terminal; read_input_line
** suppresses it and avoids readline's echo when input is piped in. */
static char	*heredoc_readline(void)
{
	return (read_input_line("> "));
}

/* A quoted delimiter (<<"EOF") keeps the body literal. Otherwise every
** line is expanded with the same rules as an unquoted word, which is why
** a zeroed quotes array is passed: no character is inside quotes. */
static void	write_heredoc_line(int fd, char *line, t_redir *redir, t_shell *sh)
{
	char	*out;
	char	*quotes;

	if (redir->quoted_delim)
	{
		ft_putendl_fd(line, fd);
		return ;
	}
	quotes = ft_calloc(ft_strlen(line) + 1, 1);
	if (!quotes)
		return ;
	out = expand_word(line, quotes, sh);
	free(quotes);
	if (out)
		ft_putendl_fd(out, fd);
	free(out);
}

/* The body is buffered into a pipe rather than a temp file, so nothing
** has to be cleaned up from disk. ctrl-C during the read closes stdin,
** which makes readline return NULL and lets us abandon the whole line. */
static int	read_heredoc(t_redir *redir, t_shell *sh)
{
	int		fds[2];
	char	*line;

	if (pipe(fds) < 0)
		return (-1);
	g_signal = 0;
	signals_setup_heredoc();
	line = heredoc_readline();
	while (line && !delim_matches(line, redir->filename))
	{
		write_heredoc_line(fds[1], line, redir, sh);
		free(line);
		line = heredoc_readline();
	}
	free(line);
	close(fds[1]);
	if (g_signal == SIGINT)
	{
		close(fds[0]);
		return (-1);
	}
	redir->heredoc_fd = fds[0];
	return (0);
}

/* All heredocs in the line are drained before anything forks, because
** they read from the terminal and only one reader can own it at a time.
** stdin is saved so an interrupted heredoc leaves the shell usable. */
int	collect_heredocs(t_cmd *cmds, t_shell *sh)
{
	t_redir	*redir;
	int		saved_stdin;
	int		failed;

	saved_stdin = dup(STDIN_FILENO);
	failed = 0;
	while (cmds && !failed)
	{
		redir = cmds->redirs;
		while (redir && !failed)
		{
			if (redir->type == R_HEREDOC && read_heredoc(redir, sh) < 0)
				failed = 1;
			redir = redir->next;
		}
		cmds = cmds->next;
	}
	dup2(saved_stdin, STDIN_FILENO);
	close(saved_stdin);
	if (failed)
		return (-1);
	return (0);
}
