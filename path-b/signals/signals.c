/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   signals.c                                          :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 23:59:48 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/05 15:30:13 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

/* The single global the subject permits. It holds nothing but a signal
** number, so the handler never touches shell data structures. */
volatile sig_atomic_t	g_signal = 0;

/* Interactive ctrl-C: bash abandons the current input and draws a fresh
** prompt. rl_replace_line clears readline's buffer, rl_on_new_line tells
** it the cursor moved, and rl_redisplay paints the empty prompt again. */
static void	handle_sigint(int sig)
{
	g_signal = sig;
	write(STDOUT_FILENO, "\n", 1);
	rl_replace_line("", 0);
	rl_on_new_line();
	rl_redisplay();
}

/* During a heredoc there is no readline prompt to repaint. Closing stdin
** is what makes the blocked read() return so the reader can bail out. */
static void	handle_heredoc_sigint(int sig)
{
	g_signal = sig;
	write(STDOUT_FILENO, "\n", 1);
	close(STDIN_FILENO);
}

void	signals_setup_interactive(void)
{
	struct sigaction	act;

	ft_bzero(&act, sizeof(act));
	act.sa_handler = handle_sigint;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_RESTART;
	sigaction(SIGINT, &act, NULL);
	act.sa_handler = SIG_IGN;
	sigaction(SIGQUIT, &act, NULL);
}

void	signals_setup_heredoc(void)
{
	struct sigaction	act;

	ft_bzero(&act, sizeof(act));
	act.sa_handler = handle_heredoc_sigint;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);
	act.sa_handler = SIG_IGN;
	sigaction(SIGQUIT, &act, NULL);
}
