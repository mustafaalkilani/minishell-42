/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/16 11:28:08 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/17 16:55:59 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "minishell.h"

static void	shell_init(t_shell *sh, char **envp)
{
	sh->env = env_init(envp);
	sh->last_status = 0;
	sh->stdin_backup = -1;
	sh->stdout_backup = -1;
	sh->current_cmds = NULL;
	sh->current_line = NULL;
}

static t_cmd	*build_cmds(char *line, t_shell *sh)
{
	t_token	*tokens;
	t_cmd	*cmds;

	tokens = lex(line);
	if (!tokens)
		return (NULL);
	if (expand(tokens, sh) != 0)
	{
		free_tokens(tokens);
		return (NULL);
	}
	cmds = parse(tokens);
	free_tokens(tokens);
	return (cmds);
}

static void	process_line(char *line, t_shell *sh)
{
	t_cmd	*cmds;

	if (line_is_blank(line))
		return ;
	cmds = build_cmds(line, sh);
	if (!cmds)
	{
		sh->last_status = EXIT_MISUSE;
		return ;
	}
	sh->last_status = exec_line(cmds, sh);
	free_cmds(cmds);
}

static int	shell_loop(t_shell *sh)
{
	char	*line;

	while (1)
	{
		signals_setup_interactive();
		line = read_input_line(PROMPT);
		if (!line)
		{
			if (isatty(STDIN_FILENO))
				ft_putendl_fd("exit", STDERR_FILENO);
			break ;
		}
		sh->current_line = line;
		if (g_signal == SIGINT)
		{
			sh->last_status = EXIT_SIG_BASE + SIGINT;
			g_signal = 0;
		}
		if (*line)
			add_history(line);
		process_line(line, sh);
		free(line);
		sh->current_line = NULL;
	}
	return (sh->last_status);
}

int	main(int argc, char **argv, char **envp)
{
	t_shell	sh;
	int		status;

	(void)argv;
	if (argc != 1)
	{
		ft_putendl_fd("minishell: takes no arguments", STDERR_FILENO);
		return (EXIT_MISUSE);
	}
	shell_init(&sh, envp);
	status = shell_loop(&sh);
	env_free(sh.env);
	rl_clear_history();
	return (status);
}
