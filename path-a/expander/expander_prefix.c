/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   expander_prefix.c                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/08 23:34:49 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/09 02:15:17 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

size_t	tilde_prefix(t_exp *e, const char *value, const char *quotes,
		int enabled)
{
	char	*home;

	if (!enabled || value[0] != '~' || quotes[0] != Q_NONE)
		return (0);
	if (value[1] && value[1] != '/')
		return (0);
	home = env_get(e->sh->env, "HOME");
	if (!home)
		return (0);
	exp_append(e, ft_strdup(home), '0');
	return (1);
}

int	is_quote_prefix(const char *quotes, size_t i)
{
	if ((quotes[i] & Q_MASK) != Q_NONE)
		return (0);
	return ((quotes[i + 1] & Q_BREAK) != 0);
}

int	braced_name_len(const char *value, const char *quotes)
{
	int	len;

	if (value[2] == '?' && value[3] == '}')
		return (1);
	len = var_name_len(value + 2, quotes + 2);
	if (len == 0 || value[2 + (size_t)len] != '}')
		return (0);
	return (len);
}

size_t	append_braced(t_exp *e, const char *value, const char *quotes,
		char flag)
{
	int		len;
	char	*name;

	len = braced_name_len(value, quotes);
	if (len == 0)
	{
		exp_append(e, ft_strdup("$"), '0');
		return (1);
	}
	if (value[2] == '?')
	{
		exp_append(e, ft_itoa(e->sh->last_status), flag);
		return (4);
	}
	name = ft_substr(value, 2, (size_t)len);
	if (!name)
		exp_append(e, NULL, flag);
	else
	{
		exp_append(e, var_lookup(name, e->sh), flag);
		free(name);
	}
	return (3 + (size_t)len);
}
