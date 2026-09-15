/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   builtin_export_list.c                              :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/26 12:04:49 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/26 20:15:12 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_b.h"

/* A variable declared but never assigned prints without the ="..." part,
** which is how bash distinguishes `export X` from `export X=`. */
static void	print_declare(t_env *node)
{
	ft_putstr_fd("declare -x ", STDOUT_FILENO);
	ft_putstr_fd(node->key, STDOUT_FILENO);
	if (node->value)
	{
		ft_putstr_fd("=\"", STDOUT_FILENO);
		ft_putstr_fd(node->value, STDOUT_FILENO);
		ft_putstr_fd("\"", STDOUT_FILENO);
	}
	ft_putchar_fd('\n', STDOUT_FILENO);
}

static int	env_size(t_env *env)
{
	int	n;

	n = 0;
	while (env)
	{
		n++;
		env = env->next;
	}
	return (n);
}

/* Bubble sort over an array of pointers. Sorting the array instead of
** the list keeps insertion order intact for `env`, which is unsorted. */
static void	sort_env_array(t_env **arr, int n)
{
	int		i;
	int		j;
	t_env	*tmp;

	i = 0;
	while (i < n - 1)
	{
		j = 0;
		while (j < n - 1 - i)
		{
			if (ft_strncmp(arr[j]->key, arr[j + 1]->key,
					ft_strlen(arr[j]->key) + 1) > 0)
			{
				tmp = arr[j];
				arr[j] = arr[j + 1];
				arr[j + 1] = tmp;
			}
			j++;
		}
		i++;
	}
}

int	export_list(t_env *env)
{
	t_env	**arr;
	int		n;
	int		i;

	n = env_size(env);
	if (n == 0)
		return (EXIT_OK);
	arr = malloc(sizeof(t_env *) * (size_t)n);
	if (!arr)
		return (1);
	i = 0;
	while (env)
	{
		arr[i] = env;
		env = env->next;
		i++;
	}
	sort_env_array(arr, n);
	i = 0;
	while (i < n)
		print_declare(arr[i++]);
	free(arr);
	return (EXIT_OK);
}
