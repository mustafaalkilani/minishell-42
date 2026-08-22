/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   expander_prefix.c                                  :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/08/04 00:00:00 by malkilan          #+#    #+#             */
/*   Updated: 2026/08/04 00:00:00 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../path_a.h"

/* A leading tilde becomes $HOME only when it is unquoted and either
** stands alone or is followed by a slash. Requiring Q_NONE with no
** Q_BREAK keeps "~" and ''~ literal, and the slash rule leaves ~user
** alone, since other users' home directories are not resolved here.
** The result is never splittable: bash does not split a tilde
** expansion even when HOME contains spaces. enabled is false for heredoc
** bodies, which bash does not tilde-expand at all.
** Returns how many characters of the word were consumed. */
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

/* An unquoted $ sitting directly in front of an opening quote is bash's
** $"..." / $'...' prefix. We have no locale catalogue and no escapes to
** decode, so the $ simply disappears and the quoted text stands.
** Q_BREAK on the next character is the whole test: it means a quote
** delimiter was dropped between the $ and that character. Looking at the
** next character's own quote state instead would miss $""x, where the
** empty quotes leave the following x unquoted but still marked. */
int	is_quote_prefix(const char *quotes, size_t i)
{
	if ((quotes[i] & Q_MASK) != Q_NONE)
		return (0);
	return ((quotes[i + 1] & Q_BREAK) != 0);
}
