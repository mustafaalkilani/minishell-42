/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_substr.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: malkilan <malkilan@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/19 14:45:14 by malkilan          #+#    #+#             */
/*   Updated: 2025/08/27 16:28:45 by malkilan         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

// #include <stdio.h>

char	*ft_substr(char const *s, unsigned int start, size_t len)
{
	size_t	i;
	size_t	str_len;
	char	*sub;

	if (!s)
		return (NULL);
	str_len = 0;
	while (s[str_len] != 0)
		str_len++;
	if (start >= str_len)
		return (ft_strdup(""));
	if (len > str_len - start)
		len = str_len - start;
	sub = malloc(len + 1);
	if (!sub)
		return (NULL);
	i = 0;
	while (i < len)
	{
		sub[i] = s[start + i];
		i++;
	}
	sub[i] = '\0';
	return (sub);
}

// int main(void)
// {
//     char s[] = "MustafaAlkilani";
//     int start = 7;
//     int len = 8;
//     printf("%s\n", s);
//     printf("%s", ft_substr(s, start, len));
// }
