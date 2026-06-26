/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_strdup.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/19 14:38:44 by malkilan          #+#    #+#             */
/*   Updated: 2025/08/19 14:44:10 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

// #include <stdio.h>

char	*ft_strdup(const char *s)
{
	size_t	len;
	size_t	i;
	char	*dup;

	len = 0;
	while (s[len])
		len++;
	dup = malloc(len + 1);
	if (!dup)
		return (NULL);
	i = 0;
	while (i < len)
	{
		dup[i] = s[i];
		i++;
	}
	dup[i] = '\0';
	return (dup);
}

// int main()
// {
//     char source[] = "MustafaAlkilani";

//     char* target = ft_strdup(source);

//     printf("%s", target);
//     return (0);
// }