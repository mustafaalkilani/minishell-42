/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   exec_wait.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: rabdalqa <rabdalqa@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/12 13:56:06 by rabdalqa          #+#    #+#             */
/*   Updated: 2026/08/13 13:38:13 by rabdalqa         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

int	status_to_exit(int status)
{
	if (WIFSIGNALED(status))
		return (EXIT_SIG_BASE + WTERMSIG(status));
	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	return (EXIT_OK);
}

static void	report_signal(int status)
{
	int	sig;

	if (!WIFSIGNALED(status))
		return ;
	sig = WTERMSIG(status);
	if (sig == SIGINT)
		write(STDOUT_FILENO, "\n", 1);
	else if (sig == SIGQUIT)
		write(STDOUT_FILENO, "Quit (core dumped)\n", 19);
}

int	wait_for_all(pid_t last_pid)
{
	int		status;
	int		last_status;
	pid_t	pid;

	last_status = EXIT_OK;
	pid = waitpid(-1, &status, 0);
	while (pid > 0)
	{
		if (pid == last_pid)
		{
			last_status = status_to_exit(status);
			report_signal(status);
		}
		pid = waitpid(-1, &status, 0);
	}
	return (last_status);
}
