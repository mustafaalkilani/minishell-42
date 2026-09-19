/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   builtin_unset.c                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: rabdalqa <rabdalqa@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/09/10 17:01:05 by rabdalqa          #+#    #+#             */
/*   Updated: 2026/09/11 22:11:03 by rabdalqa         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

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
