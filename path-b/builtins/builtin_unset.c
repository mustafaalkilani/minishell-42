/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   builtin_unset.c                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

/* A leading '-' on the first argument is read as an option flag, and we
** support none, so bash aborts before unsetting anything and exits 2.
** Later arguments are operands, and unset ignores malformed names, so
** `unset A -x` succeeds silently. */
static int	invalid_option(const char *arg)
{
	char	opt[3];

	if (arg[0] != '-' || !arg[1])
		return (0);
	opt[0] = '-';
	opt[1] = arg[1];
	opt[2] = '\0';
	shell_error("unset", opt, "invalid option");
	ft_putendl_fd("unset: usage: unset [-f] [-v] [name ...]", STDERR_FILENO);
	return (1);
}

/* Neither a missing name nor a malformed one is an error: bash skips
** anything that could not name a variable and still reports success. */
int	builtin_unset(char **argv, t_shell *sh)
{
	int	i;

	if (argv[1] && invalid_option(argv[1]))
		return (EXIT_MISUSE);
	i = 1;
	while (argv[i])
	{
		if (env_key_is_valid(argv[i]) && !ft_strchr(argv[i], '='))
			env_unset(&sh->env, argv[i]);
		i++;
	}
	return (EXIT_OK);
}
