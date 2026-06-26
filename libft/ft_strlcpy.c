/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ft_strlcpy.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mustafa <mustafa@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/08/10 19:45:06 by malkilan          #+#    #+#             */
/*   Updated: 2025/08/26 17:07:47 by mustafa          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "libft.h"

size_t	ft_strlcpy(char *dst, const char *src, size_t size)
{
	size_t	i;
	size_t	src_len;

	src_len = 0;
	i = 0;
	while (src[src_len] != 0)
		src_len++;
	if (size == 0)
		return (src_len);
	while (src[i] != 0 && i < size - 1)
	{
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
	return (src_len);
}

/*
int	main(void)
{
	char	src[];
	char dst[20];
	int		len;

	src[] = "Hello, world!";
	len = ft_strlcpy(dst, src, sizeof(dst));
	printf("Copied string: '%s'\n", dst);
	printf("Length of src: %d\n", len);
	return (0);
}*/
