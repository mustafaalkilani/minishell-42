/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   builtin_exit.c                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

/* bash validates the argument with strtol, which skips leading blanks,
** then accepts only blanks after the digits. So `exit " 42 "` is 42
** while `exit "42 x"` is a numeric-argument error. */
static const char	*skip_blanks(const char *s)
{
	while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
		s++;
	return (s);
}

static int	is_numeric_arg(const char *s)
{
	int	i;

	s = skip_blanks(s);
	i = 0;
	if (s[i] == '+' || s[i] == '-')
		i++;
	if (!ft_isdigit(s[i]))
		return (0);
	while (ft_isdigit(s[i]))
		i++;
	return (*skip_blanks(s + i) == '\0');
}

/* Overflow is detected before it happens by comparing against the limit
** divided by ten. bash treats an out-of-range number exactly like a
** non-numeric one, so both return 0 here. */
static int	parse_exit_code(const char *s, long long *out)
{
	unsigned long long	acc;
	unsigned long long	limit;
	int					neg;
	int					i;

	if (!is_numeric_arg(s))
		return (0);
	s = skip_blanks(s);
	neg = (s[0] == '-');
	i = 0;
	if (s[0] == '+' || s[0] == '-')
		i++;
	limit = 9223372036854775807ULL + (unsigned long long)neg;
	acc = 0;
	while (ft_isdigit(s[i]))
	{
		if (acc > (limit - (unsigned long long)(s[i] - '0')) / 10)
			return (0);
		acc = acc * 10 + (unsigned long long)(s[i] - '0');
		i++;
	}
	*out = (long long)acc;
	if (neg)
		*out = -*out;
	return (1);
}

/* Frees everything the shell owns before leaving, so valgrind stays
** quiet about our own allocations. readline's internal buffers are the
** documented exception the subject allows. */
static void	shell_cleanup(t_shell *sh)
{
	free(sh->current_line);
	sh->current_line = NULL;
	free_cmds(sh->current_cmds);
	sh->current_cmds = NULL;
	env_free(sh->env);
	sh->env = NULL;
	rl_clear_history();
}

/* "exit 1 2" is the one case that does not exit: bash reports the error,
** returns 1 and keeps the shell alive. */
int	builtin_exit(char **argv, t_shell *sh)
{
	long long	code;

	if (isatty(STDIN_FILENO))
		ft_putendl_fd("exit", STDERR_FILENO);
	if (!argv[1])
	{
		code = sh->last_status;
		shell_cleanup(sh);
		exit((int)code);
	}
	if (!parse_exit_code(argv[1], &code))
	{
		shell_error("exit", argv[1], "numeric argument required");
		shell_cleanup(sh);
		exit(EXIT_MISUSE);
	}
	if (argv[2])
	{
		shell_error("exit", NULL, "too many arguments");
		return (1);
	}
	shell_cleanup(sh);
	exit((int)(((code % 256) + 256) % 256));
}
