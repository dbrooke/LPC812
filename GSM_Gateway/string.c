//Functions taken from minilib-c https://code.google.com/p/minilib-c/

typedef unsigned long size_t;
#define NULL ((void*)0)

char *strcpy(char *dst0, const char *src0)
{
    char *s = dst0;
    
    while ((*dst0++ = *src0++))
        ;
    
    return s;
}

char *strstr(const char *searchee, const char *lookfor)
{
    /* Less code size, but quadratic performance in the worst case.  */
    if (*searchee == 0)
    {
        if (*lookfor)
            return (char *) NULL;
        return (char *) searchee;
    }
    
    while (*searchee)
    {
        size_t i;
        i = 0;
        
        while (1)
        {
            if (lookfor[i] == 0)
            {
                return (char *) searchee;
            }
            
            if (lookfor[i] != searchee[i])
            {
                break;
            }
            i++;
        }
        searchee++;
    }
    
    return (char *) NULL;
}

char *strchr(const char *s1, int i)
{
    const unsigned char *s = (const unsigned char *)s1;
    unsigned char c = i;
    
    while (*s && *s != c)
        s++;
    if (*s == c)
        return (char *)s;
    return NULL;
}

char *strcat(char *s1, const char *s2)
{
    char *s = s1;
    
    while (*s1)
        s1++;
    
    while ((*s1++ = *s2++))
        ;
    return s;
}

size_t strlen(const char *str)
{
    const char *start = str;
    
    while (*str)
        str++;
    return str - start;
}
